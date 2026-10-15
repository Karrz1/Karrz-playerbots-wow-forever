<#
.SYNOPSIS
  Reads WowB.exe (read-only) and writes a small jamre export: the switch jump tables of the client's message dispatch and
  the code at their case targets. No install needed (PowerShell 5.1 + the .NET compiler that ships with Windows).
.EXAMPLE
  .\Export-JamTables.ps1 -Exe "D:\wow\_classic_beta_\WowB.exe" -Out "$env:TEMP\wowb-70291.json.gz" -Build 70291
  .\Export-JamTables.ps1 -Exe "F:\WowForeverClient-backup\_classic_beta_\WowB.exe" -Out "$env:TEMP\wowb-70205.json.gz" -Build 70205
  The two .json.gz files (a few MB each) are what the analysis needs; the executables themselves do not leave the PC.
#>
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$MinCases = 8,
    [string]$Build = ""
)

$source = @'
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;

public static class JamExport
{
    class Sec { public string Name; public uint Rva; public byte[] Data; public bool Exec; }
    class Table { public uint LoadRva; public uint TableRva; public long ByteTableRva; public int Cases; public bool BoundFromCmp; public uint[] Targets; }

    static List<Sec> secs = new List<Sec>();
    static ulong imageBase;

    static Sec SectionAt(uint rva)
    {
        for (int i = 0; i < secs.Count; i++)
        {
            Sec s = secs[i];
            if (rva >= s.Rva && rva < s.Rva + (uint)s.Data.Length) return s;
        }
        return null;
    }
    static bool IsCode(uint rva) { Sec s = SectionAt(rva); return s != null && s.Exec; }
    static bool U32(uint rva, out uint v)
    {
        v = 0; Sec s = SectionAt(rva);
        if (s == null) return false;
        long o = rva - s.Rva;
        if (o + 4 > s.Data.Length) return false;
        v = BitConverter.ToUInt32(s.Data, (int)o);
        return true;
    }
    static int U8(uint rva)
    {
        Sec s = SectionAt(rva);
        if (s == null) return -1;
        long o = rva - s.Rva;
        if (o >= s.Data.Length) return -1;
        return s.Data[o];
    }

    static void LoadPe(string path)
    {
        byte[] f = File.ReadAllBytes(path);
        int pe = BitConverter.ToInt32(f, 0x3C);
        if (f[pe] != 'P' || f[pe + 1] != 'E') throw new Exception("not a PE file");
        int nsec = BitConverter.ToUInt16(f, pe + 6);
        int optSize = BitConverter.ToUInt16(f, pe + 20);
        int opt = pe + 24;
        if (BitConverter.ToUInt16(f, opt) != 0x20B) throw new Exception("not a PE32+ (x64) image");
        imageBase = BitConverter.ToUInt64(f, opt + 24);
        int sh = opt + optSize;
        for (int i = 0; i < nsec; i++)
        {
            int h = sh + 40 * i;
            string name = Encoding.ASCII.GetString(f, h, 8).TrimEnd('\0');
            uint vsize = BitConverter.ToUInt32(f, h + 8);
            uint va = BitConverter.ToUInt32(f, h + 12);
            uint rsize = BitConverter.ToUInt32(f, h + 16);
            uint rptr = BitConverter.ToUInt32(f, h + 20);
            uint ch = BitConverter.ToUInt32(f, h + 36);
            uint len = Math.Max(vsize, rsize);
            byte[] data = new byte[len];
            long avail = Math.Max(0L, Math.Min((long)rsize, (long)f.Length - rptr));
            if (avail > 0) Array.Copy(f, (long)rptr, data, 0L, Math.Min(avail, (long)len));
            Sec s = new Sec();
            s.Name = name; s.Rva = va; s.Data = data;
            s.Exec = (ch & 0x20000000u) != 0 || (ch & 0x20u) != 0;
            secs.Add(s);
        }
    }

    static bool IsModrmDword(byte b) { return b == 0x84 || b == 0x8C || b == 0x94 || b == 0x9C || b == 0xA4 || b == 0xAC || b == 0xB4 || b == 0xBC; }

    // largest-index bound from the closest cmp reg, imm in the 96 bytes before the load (same rules as jamre/jumptables.py)
    static int FindBound(byte[] code, int loadOff)
    {
        int lo = Math.Max(0, loadOff - 96);
        int best = -1;
        int i = lo;
        while (i < loadOff)
        {
            int v = 0; bool hit = false; int adv = 1;
            if (code[i] == 0x83 && i + 3 <= loadOff && code[i + 1] >= 0xF8) { v = (sbyte)code[i + 2]; hit = true; adv = 3; }
            else if (code[i] == 0x81 && i + 6 <= loadOff && code[i + 1] >= 0xF8) { v = BitConverter.ToInt32(code, i + 2); hit = true; adv = 6; }
            else if (code[i] == 0x3D && i + 5 <= loadOff) { v = BitConverter.ToInt32(code, i + 1); hit = true; adv = 5; }
            if (hit && v >= 0 && v < 100000) best = v;
            i += adv;
        }
        return best;
    }

    static long FindByteTable(byte[] code, int loadOff)
    {
        int lo = Math.Max(0, loadOff - 48);
        long last = -1;
        int i = lo;
        while (i + 8 <= loadOff)
        {
            if (code[i] == 0x0F && code[i + 1] == 0xB6 && IsModrmDword(code[i + 2]) && code[i + 3] < 0x40)
            {
                uint disp = BitConverter.ToUInt32(code, i + 4);
                Sec s = SectionAt(disp);
                if (s != null && !s.Exec) last = disp;
                i += 8;
            }
            else i++;
        }
        return last;
    }

    static int RunLength(uint tableRva, int limit)
    {
        int n = 0;
        while (n < limit)
        {
            uint v;
            if (!U32((uint)(tableRva + 4 * n), out v) || !IsCode(v)) break;
            n++;
        }
        return n;
    }

    static List<Table> FindTables(int minCases)
    {
        List<Table> outl = new List<Table>();
        HashSet<string> seen = new HashSet<string>();
        foreach (Sec sec in secs)
        {
            if (!sec.Exec) continue;
            byte[] code = sec.Data;
            for (int p = 0; p + 7 <= code.Length; p++)
            {
                if (code[p] != 0x8B || !IsModrmDword(code[p + 1]) || (code[p + 2] & 0xC0) != 0x80) continue;
                uint disp = BitConverter.ToUInt32(code, p + 3);
                Sec ds = SectionAt(disp);
                if (ds == null || ds.Exec) continue;
                uint first;
                if (!U32(disp, out first) || !IsCode(first)) continue;
                int loadOff = (p > 0 && code[p - 1] >= 0x40 && code[p - 1] <= 0x4F) ? p - 1 : p;
                int bound = FindBound(code, loadOff);
                long byteTbl = FindByteTable(code, loadOff);
                int cases; bool fromCmp; uint[] targets;
                if (byteTbl >= 0)
                {
                    if (bound < 0) continue;
                    int ncases = bound + 1;
                    int[] idx = new int[ncases];
                    bool bad = false; int max = 0;
                    for (int k = 0; k < ncases; k++) { idx[k] = U8((uint)(byteTbl + k)); if (idx[k] < 0) { bad = true; break; } if (idx[k] > max) max = idx[k]; }
                    if (bad) continue;
                    int run = RunLength(disp, max + 1);
                    if (run <= max) continue;
                    targets = new uint[ncases];
                    for (int k = 0; k < ncases; k++) { uint t; U32((uint)(disp + 4 * idx[k]), out t); targets[k] = t; }
                    cases = ncases; fromCmp = true;
                }
                else
                {
                    int run = RunLength(disp, 100000);
                    if (bound >= 0 && bound + 1 <= run) { cases = bound + 1; fromCmp = true; }
                    else { cases = run; fromCmp = false; }
                    targets = new uint[cases];
                    for (int k = 0; k < cases; k++) { uint t; U32((uint)(disp + 4 * k), out t); targets[k] = t; }
                }
                if (cases < minCases) continue;
                uint loadRva = (uint)(sec.Rva + loadOff);
                string key = loadRva.ToString() + ":" + disp.ToString();
                if (seen.Contains(key)) continue;
                seen.Add(key);
                Table t2 = new Table();
                t2.LoadRva = loadRva; t2.TableRva = disp; t2.ByteTableRva = byteTbl; t2.Cases = cases; t2.BoundFromCmp = fromCmp; t2.Targets = targets;
                outl.Add(t2);
            }
        }
        outl.Sort(delegate (Table a, Table b) { return b.Cases.CompareTo(a.Cases); });
        return outl;
    }

    const int Chunk = 512;

    static byte[] ReadChunk(uint rva)
    {
        Sec s = SectionAt(rva);
        if (s == null) return null;
        long o = rva - s.Rva;
        long n = Math.Min((long)Chunk, (long)s.Data.Length - o);
        if (n <= 0) return null;
        byte[] b = new byte[n];
        Array.Copy(s.Data, o, b, 0L, n);
        return b;
    }

    public static string Run(string exe, string outPath, int minCases, string build)
    {
        LoadPe(exe);
        List<Table> tables = FindTables(minCases);
        SortedDictionary<uint, byte[]> chunks = new SortedDictionary<uint, byte[]>();
        HashSet<uint> frontier = new HashSet<uint>();
        foreach (Table t in tables) foreach (uint x in t.Targets) frontier.Add(x);
        for (int level = 0; level < 3; level++)
        {
            HashSet<uint> next = new HashSet<uint>();
            List<uint> list = new List<uint>(frontier);
            list.Sort();
            foreach (uint rva in list)
            {
                if (chunks.ContainsKey(rva)) continue;
                byte[] data = ReadChunk(rva);
                if (data == null) continue;
                chunks[rva] = data;
                // direct calls: E8 rel32 (byte scan, no disassembler; false hits only add a few unused chunks)
                for (int q = 0; q + 5 <= data.Length; q++)
                {
                    if (data[q] != 0xE8) continue;
                    long tgt = (long)rva + q + 5 + BitConverter.ToInt32(data, q + 1);
                    if (tgt > 0 && tgt < uint.MaxValue && IsCode((uint)tgt) && !chunks.ContainsKey((uint)tgt)) next.Add((uint)tgt);
                }
            }
            frontier = next;
        }
        StringBuilder sb = new StringBuilder();
        sb.Append("{\"format\":\"jamre-export-v1\",\"build\":\"").Append(build).Append("\",\"image_base\":").Append(imageBase.ToString()).Append(",\"tables\":[");
        for (int i = 0; i < tables.Count; i++)
        {
            Table t = tables[i];
            if (i > 0) sb.Append(',');
            sb.Append("{\"table_rva\":").Append(t.TableRva).Append(",\"cases\":").Append(t.Cases)
              .Append(",\"byte_table_rva\":").Append(t.ByteTableRva >= 0 ? t.ByteTableRva.ToString() : "null")
              .Append(",\"load_rva\":").Append(t.LoadRva).Append(",\"bound_from_cmp\":").Append(t.BoundFromCmp ? "true" : "false").Append(",\"targets\":[");
            for (int k = 0; k < t.Targets.Length; k++) { if (k > 0) sb.Append(','); sb.Append(t.Targets[k]); }
            sb.Append("]}");
        }
        sb.Append("],\"chunks\":[");
        bool firstChunk = true;
        foreach (KeyValuePair<uint, byte[]> kv in chunks)
        {
            if (!firstChunk) sb.Append(',');
            firstChunk = false;
            sb.Append("{\"rva\":").Append(kv.Key).Append(",\"b64\":\"").Append(Convert.ToBase64String(kv.Value)).Append("\"}");
        }
        sb.Append("]}");
        byte[] raw = Encoding.ASCII.GetBytes(sb.ToString());
        using (FileStream fs = new FileStream(outPath, FileMode.Create))
        using (GZipStream gz = new GZipStream(fs, CompressionMode.Compress))
            gz.Write(raw, 0, raw.Length);
        StringBuilder rep = new StringBuilder();
        rep.AppendLine("image base 0x" + imageBase.ToString("X"));
        rep.AppendLine("tables " + tables.Count + ", chunks " + chunks.Count + ", json bytes " + raw.Length);
        for (int i = 0; i < tables.Count && i < 25; i++)
            rep.AppendLine("  table 0x" + tables[i].TableRva.ToString("X") + " cases " + tables[i].Cases + (tables[i].ByteTableRva >= 0 ? " (byte-index)" : ""));
        return rep.ToString();
    }
}
'@

Add-Type -TypeDefinition $source -Language CSharp
if (-not (Test-Path -LiteralPath $Exe)) { throw "Exe not found: $Exe" }
Write-Host ([JamExport]::Run($Exe, $Out, $MinCases, $Build))
Write-Host "Wrote $Out ($((Get-Item -LiteralPath $Out).Length) bytes)"

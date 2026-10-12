<#
.SYNOPSIS
  Reads the sections of the RUNNING WowB.exe (read-only: PROCESS_VM_READ, no debugger, no injection) and writes them to a
  gzip file for offline analysis. The client keeps its code encrypted on disk and decrypts it while it loads, so the
  executable file cannot be analysed statically; the loaded image can.
.DESCRIPTION
  Start the client, log in to the realm and, best, have been in a party and a battleground so the message readers have
  run, then run this once. The output stays on this PC until it is copied on purpose. It holds Blizzard code: never
  commit it, never share it.
  The client decrypts code pages on demand, the first time they run, and a decrypted page stays readable. One snapshot
  therefore only shows what has run so far. With -Seconds N the script keeps polling the pages it could not read and merges
  them as they appear, so play normally for N seconds (log in, invite bots into a group, enter a battleground, inspect,
  whisper, open the auction house) and the dump covers everything that ran meanwhile.
.EXAMPLE
  .\Dump-WowBMemory.ps1 -Out "$env:TEMP\wowb-70291-live.mem.gz" -Build 70291 -Seconds 900
#>
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$Build = "",
    [int]$ProcessId = 0,
    [int]$Seconds = 0,
    [int]$IntervalMs = 1000
)

$source = @'
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Text;

public static class WowMemDump
{
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    const uint PROCESS_VM_READ = 0x0010, PROCESS_QUERY_INFORMATION = 0x0400;

    static bool Read(IntPtr h, long addr, byte[] buf, int size)
    {
        IntPtr got;
        return ReadProcessMemory(h, new IntPtr(addr), buf, new IntPtr(size), out got) && got.ToInt64() == size;
    }

    public static string Run(int pid, string outPath, string build, int seconds, int intervalMs)
    {
        Process p = Process.GetProcessById(pid);
        long baseAddr = p.MainModule.BaseAddress.ToInt64();
        IntPtr h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, false, pid);
        if (h == IntPtr.Zero) throw new Exception("OpenProcess failed, error " + Marshal.GetLastWin32Error());
        try
        {
            byte[] hdr = new byte[0x1000];
            if (!Read(h, baseAddr, hdr, hdr.Length)) throw new Exception("cannot read the PE header");
            int pe = BitConverter.ToInt32(hdr, 0x3C);
            if (BitConverter.ToUInt32(hdr, pe) != 0x00004550) throw new Exception("not a PE image");
            int nsec = BitConverter.ToUInt16(hdr, pe + 6);
            int optSize = BitConverter.ToUInt16(hdr, pe + 20);
            int secTable = pe + 24 + optSize;
            long headerBase = BitConverter.ToInt64(hdr, pe + 24 + 24);

            StringBuilder json = new StringBuilder();
            json.Append("{\"build\":\"" + build + "\",\"pid\":" + pid + ",\"runtime_base\":" + baseAddr + ",\"image_base\":" + headerBase + ",\"sections\":[");
            byte[][] datas = new byte[nsec][];
            bool[][] got = new bool[nsec][];
            string[] names = new string[nsec];
            uint[] rvas = new uint[nsec];
            bool[] execs = new bool[nsec];
            for (int i = 0; i < nsec; i++)
            {
                int o = secTable + 40 * i;
                names[i] = Encoding.ASCII.GetString(hdr, o, 8).TrimEnd('\0');
                uint vsize = BitConverter.ToUInt32(hdr, o + 8);
                rvas[i] = BitConverter.ToUInt32(hdr, o + 12);
                uint chr = BitConverter.ToUInt32(hdr, o + 36);
                execs[i] = (chr & 0x20000000) != 0 || (chr & 0x20) != 0;
                int len = (int)((vsize + 0xFFF) & ~0xFFFu);
                datas[i] = new byte[len];
                got[i] = new bool[len / 0x1000];
            }
            DateTime end = DateTime.UtcNow.AddSeconds(seconds);
            DateTime nextReport = DateTime.UtcNow;
            byte[] page = new byte[0x1000];
            while (true)
            {
                int missing = 0, newly = 0;
                for (int i = 0; i < nsec; i++)
                    for (int pg = 0; pg < got[i].Length; pg++)
                    {
                        if (got[i][pg]) continue;
                        if (Read(h, baseAddr + rvas[i] + (long)pg * 0x1000, page, 0x1000))
                        {
                            Buffer.BlockCopy(page, 0, datas[i], pg * 0x1000, 0x1000);
                            got[i][pg] = true; newly++;
                        }
                        else missing++;
                    }
                if (missing == 0 || DateTime.UtcNow >= end) { Console.WriteLine("final: " + missing + " pages unreadable"); break; }
                if (DateTime.UtcNow >= nextReport)
                {
                    Console.WriteLine(DateTime.UtcNow.ToString("HH:mm:ss") + " unreadable pages " + missing + " (+" + newly + ")");
                    nextReport = DateTime.UtcNow.AddSeconds(30);
                }
                System.Threading.Thread.Sleep(intervalMs);
            }
            for (int i = 0; i < nsec; i++)
            {
                int bad = 0;
                byte[] bits = new byte[(got[i].Length + 7) / 8];
                for (int pg = 0; pg < got[i].Length; pg++) { if (got[i][pg]) bits[pg >> 3] |= (byte)(1 << (pg & 7)); else bad++; }
                if (i > 0) json.Append(",");
                json.Append("{\"name\":\"" + names[i] + "\",\"rva\":" + rvas[i] + ",\"length\":" + datas[i].Length + ",\"exec\":" + (execs[i] ? "true" : "false") + ",\"unreadable_pages\":" + bad + ",\"present_b64\":\"" + Convert.ToBase64String(bits) + "\"}");
            }
            json.Append("]}");
            byte[] head = Encoding.UTF8.GetBytes(json.ToString());
            using (FileStream fs = File.Create(outPath))
            using (GZipStream gz = new GZipStream(fs, CompressionMode.Compress))
            {
                gz.Write(Encoding.ASCII.GetBytes("WOWMEM01"), 0, 8);
                gz.Write(BitConverter.GetBytes(head.Length), 0, 4);
                gz.Write(head, 0, head.Length);
                for (int i = 0; i < nsec; i++) gz.Write(datas[i], 0, datas[i].Length);
            }
            return json.ToString();
        }
        finally { CloseHandle(h); }
    }
}
'@

Add-Type -TypeDefinition $source -Language CSharp

if ($ProcessId -eq 0) {
    $procs = @(Get-Process -Name WowB -ErrorAction SilentlyContinue)
    if ($procs.Count -ne 1) { throw "expected exactly one running WowB.exe, found $($procs.Count); pass -ProcessId" }
    $ProcessId = $procs[0].Id
}
$info = [WowMemDump]::Run($ProcessId, $Out, $Build, $Seconds, $IntervalMs)
Write-Host $info
Write-Host ("wrote {0} ({1:N1} MB)" -f $Out, ((Get-Item $Out).Length / 1MB))

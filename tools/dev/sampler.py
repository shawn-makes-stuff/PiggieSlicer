"""Sample the UI thread of the running piggie-slicer: cdb attached once, a break every interval prints
thread 0's stack and continues; after N samples cdb detaches (the app keeps running)."""
import ctypes, subprocess, sys, time
N, INTERVAL = int(sys.argv[2]), float(sys.argv[3])
pid = int(sys.argv[1])
cdb = r"C:\Program Files\WindowsApps\Microsoft.WinDbg_1.2606.22001.0_x64__8wekyb3d8bbwe\amd64\cdb.exe"
sym = r"D:\Projects\PiggieSlicer_V2\build\src\slic3r-app-launcher\Release"
cmd = (f'.reload /f piggie-slicer.exe; r $t1=0; '
       f'sxe -c "r $t1=@$t1+1; .echo ===SAMPLE; ~0 kc 45; .if (@$t1 >= 0n{N}) {{ qd }} .else {{ gc }}" bpe; g')
out = open(sys.argv[4], "w", encoding="utf-8")
proc = subprocess.Popen([cdb, "-p", str(pid), "-y", sym, "-c", cmd], stdout=out, stderr=subprocess.STDOUT)
k32 = ctypes.WinDLL("kernel32")
h = k32.OpenProcess(0x1F0FFF, False, pid)
time.sleep(25)  # attach + load symbols
for _ in range(N + 2):
    if proc.poll() is not None:
        break
    k32.DebugBreakProcess(h)
    time.sleep(INTERVAL)
proc.wait(timeout=120)
print("done")

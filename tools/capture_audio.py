#!/usr/bin/env python3
"""Explicit, bounded diagnostic capture over a Windows SSH-attached serial port.
Stores the exact post-filter mono stream used by the detector, not microphone audio.
No third-party Python packages, Arduino configuration or persistent baud changes.
"""
import argparse
import base64
from pathlib import Path
import re
import subprocess
import wave

class Capture:
    def __init__(self, output, allow_missing_prefix=False):
        self.output=Path(output)
        self.partial=self.output.with_suffix(self.output.suffix+'.partial')
        if self.output.exists() or self.partial.exists():
            raise FileExistsError('Capture output already exists')
        self.file=None
        self.samples=0
        self.start_offset=0
        self.allow_missing_prefix=allow_missing_prefix
        self.done=False
    def accept(self, line):
        if line.startswith('CAPTURE_BEGIN '):
            if self.file: raise ValueError('Duplicate capture header')
            fields=dict(re.findall(r'(\w+)=(\d+)',line))
            if fields.get('rate')!='11025' or fields.get('bits')!='16' or fields.get('baud')!='460800':
                raise ValueError('Unsupported capture format')
            self.file=wave.open(str(self.partial),'wb')
            self.file.setnchannels(1);self.file.setsampwidth(2);self.file.setframerate(11025)
        elif line.startswith('CAPTURE_ROW '):
            if not self.file: raise ValueError('Audio before header')
            _,offset,encoded=line.split(' ',2)
            data=base64.b64decode(encoded,validate=True)
            if self.allow_missing_prefix and self.samples==0 and self.start_offset==0 and int(offset) in (256,512):
                self.start_offset=int(offset)
            if int(offset)!=self.samples+self.start_offset or not data or len(data)>512 or len(data)%2:
                raise ValueError(f'Missing, duplicate or malformed audio packet: expected offset {self.samples+self.start_offset}, got {offset}, bytes={len(data)}')
            self.file.writeframesraw(data);self.samples+=len(data)//2
        elif line.startswith('CAPTURE_END '):
            expected=int(line.split('samples=',1)[1])
            if not self.file or expected!=self.samples+self.start_offset or not self.samples:
                raise ValueError('Incomplete or empty capture')
            self.file.close();self.file=None
            self.partial.rename(self.output);self.done=True
        elif line.startswith('CAPTURE_ERROR') or line.startswith('CAPTURE_NOT_READY'):
            raise RuntimeError(line)
        return self.done
    def close(self):
        if self.file: self.file.close();self.file=None


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ssh',required=True,help='Windows SSH alias')
    parser.add_argument('--port',required=True,help='Windows COM port')
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--allow-missing-prefix',action='store_true',help='Allow only the first one or two 256-sample packets to be absent; record that gap explicitly without padding')
    args=parser.parse_args()
    if not re.fullmatch(r'COM\d+',args.port,re.I): parser.error('Expected a COM port')
    if args.output.suffix.lower()!='.wav': parser.error('Expected a .wav output file')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    capture=Capture(args.output,args.allow_missing_prefix)
    log=args.output.with_suffix('.log')
    if log.exists(): raise FileExistsError('Capture log already exists')
    script=r'''$ProgressPreference='SilentlyContinue'
$port=New-Object System.IO.Ports.SerialPort 'PORT',115200,'None',8,'One'
$port.DtrEnable=$false; $port.RtsEnable=$false; $port.ReadTimeout=1000
try {
 $port.Open(); $port.DiscardInBuffer(); $port.Write('a')
 $timer=[Diagnostics.Stopwatch]::StartNew()
 while($timer.Elapsed.TotalSeconds -lt 55){
   try {$line=$port.ReadLine().Trim()} catch [System.TimeoutException] {continue}
   if($line.StartsWith('CAPTURE_BEGIN ')) {
     $port.BaudRate=460800
     [Console]::WriteLine($line)
   } elseif($line.StartsWith('CAPTURE_END ')) {
     $port.BaudRate=115200
     [Console]::WriteLine($line); break
   } elseif($line.StartsWith('CAPTURE_NOT_READY') -or $line.StartsWith('CAPTURE_ERROR')) {
     [Console]::WriteLine($line); break
   } elseif($line -match '^(CAPTURE_ROW |CAST_BASELINE |QUIET_BITE |AUTO |DETECT |BITE |HID |Alive:|Audio overflow:)') {
     [Console]::WriteLine($line)
   }
 }
} finally {if($port.IsOpen){$port.Close()}}
'''.replace('PORT',args.port)
    encoded=base64.b64encode(script.encode('utf-16le')).decode()
    proc=subprocess.Popen(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=10',
                           '-o','ServerAliveInterval=10',args.ssh,
                           'powershell -NoProfile -EncodedCommand '+encoded],
                          stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,
                          text=True,errors='replace')
    try:
        with log.open('x') as events:
            for line in proc.stdout:
                line=line.strip()
                if line.startswith('CAPTURE_'):
                    before_offset=capture.start_offset
                    complete=capture.accept(line)
                    if capture.start_offset!=before_offset:
                        gap=f'CAPTURE_PREFIX_GAP samples={capture.start_offset}; WAV starts after this gap (no padding)'
                        events.write(gap+'\n');events.flush();print(gap,flush=True)
                    if complete:
                        print(f'Capture verified: {capture.samples} samples, {capture.samples/11025:.2f}s',flush=True)
                    elif line.startswith('CAPTURE_BEGIN'):
                        print('Diagnostic capture started (40 seconds).',flush=True)
                else:
                    events.write(line+'\n');events.flush()
                    print(line,flush=True)
                    if line.startswith('Audio overflow:'):
                        raise RuntimeError('Audio overrun during capture; partial output is not valid timing evidence')
        code=proc.wait(timeout=10)
        if code or not capture.done: raise RuntimeError('Capture did not finish; partial output is not valid evidence')
    finally:
        capture.close()
        if proc.poll() is None:
            # Drain the bounded remote job so its SSH output cannot block and
            # leave the serial port held by an orphan after a validation error.
            for _line in proc.stdout: pass
            proc.wait(timeout=10)

if __name__=='__main__': main()

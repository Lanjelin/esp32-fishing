#!/usr/bin/env python3
"""Replay pinned references through resampling/lossy audio stages and reject synthetic negatives.
Run tools/build_bite_templates.py first. Requires ffmpeg and C++17.
"""
from pathlib import Path
import subprocess,random,array,math,json
root=Path(__file__).resolve().parent.parent
cache=root/'build/reference-audio'
exe=cache/'check-references'
subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(root/'tests/bite_references.cpp'),'-o',str(exe)],check=True)
def ff(*args): subprocess.run(['ffmpeg','-v','error','-y',*map(str,args)],check=True)
def test(name,pcm,rate,expected):
 r=subprocess.run([str(exe),str(pcm),str(rate),expected],capture_output=True,text=True)
 print(name, 'PASS' if r.returncode==0 else 'FAIL',r.stdout.strip(),flush=True)
 return r.returncode==0
passed=total=0
for fid in [s['id'] for s in json.loads((root/'tools/bite-reference-sources.json').read_text())]:
 source=cache/f'{fid}.ogg'
 for rate in (44100,48000):
  pcm=cache/f'{fid}-{rate}.pcm'
  ff('-i',source,'-ar',rate,'-ac',2,'-f','s16le',pcm)
  passed+=test(f'{fid} resample {rate}',pcm,rate,'match');total+=1
  # Two lossy stages approximate streaming + Bluetooth; not an exact GFN simulation.
  aac=cache/f'{fid}-{rate}.m4a';sbc=cache/f'{fid}-{rate}.sbc'
  ff('-i',source,'-ar',rate,'-ac',2,'-c:a','aac','-b:a','96k',aac)
  wav=cache/f'{fid}-{rate}-aac.wav'
  ff('-i',aac,'-c:a','pcm_s16le',wav)
  ff('-i',wav,'-c:a','sbc','-b:a','328k',sbc)
  ff('-i',sbc,'-af','volume=0.3,adelay=137:all=1','-ar',rate,'-ac',2,'-f','s16le',pcm)
  passed+=test(f'{fid} AAC+SBC gain/delay {rate}',pcm,rate,'match');total+=1
  ff('-i',sbc,'-af','volume=0.03,lowpass=f=3200,adelay=137:all=1','-ar',rate,'-ac',2,'-f','s16le',pcm)
  passed+=test(f'{fid} quiet AAC+SBC/EQ {rate}',pcm,rate,'match');total+=1
 # Held-out phases do not coincide with any construction offset (multiples of 16).
 # Audio content is unchanged; only the input-frame alignment differs.
 phase_pcm=cache/f'{fid}-phase.pcm'
 original=(cache/f'{fid}.pcm').read_bytes()
 for offset in range(8,1024,64):
  phase_pcm.write_bytes(bytes(offset*4)+original)
  passed+=test(f'{fid} held-out phase {offset}',phase_pcm,44100,'match');total+=1
 # Independent playback factors between construction factors, at held-out phases.
 for factor in (.9625,.9875,1.0125,1.0375):
  shifted=cache/f'{fid}-heldout-speed.pcm'
  ff('-i',source,'-af',f'aresample=44100,asetrate={44100*factor},aresample=44100',
     '-ac',2,'-f','s16le',shifted)
  raw_shifted=shifted.read_bytes()
  for offset in (8,264,520,776):
   phase_pcm.write_bytes(bytes(offset*4)+raw_shifted)
   passed+=test(f'{fid} held-out speed {factor} phase {offset}',phase_pcm,44100,'match');total+=1
 # Low-volume and temporal-order regressions: old detector missed all quiet
 # samples and accepted every reversed 350ms frame sequence.
 quiet=cache/f'{fid}-quiet.pcm'
 ff('-i',source,'-af','volume=0.03','-ar',44100,'-ac',2,'-f','s16le',quiet)
 passed+=test(f'{fid} quiet',quiet,44100,'match');total+=1
 values=array.array('h');values.frombytes(quiet.read_bytes());rng=random.Random(247)
 mixed=array.array('h',(max(-32768,min(32767,v+rng.randint(-57,57))) for v in values))
 mixed_path=cache/f'{fid}-quiet-noise.pcm';mixed_path.write_bytes(mixed.tobytes())
 passed+=test(f'{fid} quiet+noise',mixed_path,44100,'match');total+=1
 raw=(cache/f'{fid}.pcm').read_bytes(); values=array.array('h');values.frombytes(raw)
 decimated=values[::8]
 energies=[sum((v/32768)**2 for v in decimated[i:i+256])/256 for i in range(0,len(decimated)-255,256)]
 onset=next(i for i,e in enumerate(energies) if e>=.0004)
 blocks=[raw[(onset+i)*4096:(onset+i+1)*4096] for i in range(15)]
 reversed_path=cache/f'{fid}-reversed.pcm';reversed_path.write_bytes(b''.join(reversed(blocks))+bytes(4096*5))
 passed+=test(f'{fid} reversed order',reversed_path,44100,'reject');total+=1

for kind in ('silence','tone','white-noise','noise-bursts','noise-rise'):
 values=array.array('h');rng=random.Random(2468)
 for i in range(44100*5):
  if kind=='silence': v=0
  elif kind=='tone':v=int(6000*math.sin(i*2*math.pi*900/44100))
  else:
   v=rng.randint(-6000,6000)
   if kind=='noise-bursts': v=int(v * math.exp(-((i % 44100)/44100)*6))
   if kind=='noise-rise': v=int(v * (i % 44100)/44100)
  values.extend((v,v))
 pcm=cache/f'{kind}.pcm';pcm.write_bytes(values.tobytes())
 passed+=test(kind,pcm,44100,'reject');total+=1
print(f'Reference tests passed: {passed}/{total}')
raise SystemExit(0 if passed==total else 1)

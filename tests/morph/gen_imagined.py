#!/usr/bin/env python3
# Hand-author an "imagined" Metasurface as a ligase~ morph TEXT surface (morph_import format),
# to demonstrate the .txt schema is human-authorable. Field order matches morph_write_snap in
# src/ligase~.c: 45 ranges x (min max enabled rand_type rand_instance base_value slew invert),
# then 32 scalars, 30 discretes, then 2 scale lists (count + 128 values each).
# Key indices: amplitude = range 16; scalars[18]=smear semitone, [19]=ref_hz, [21]=moog cutoff;
# discretes[22]=smear.enabled, [23]=smear.source(1=SEMITONE), [25]=smear.ref_note(69).
NR=45
def snap_values(semitone, moog_cutoff, amp_min, amp_max):
    v=[]
    for r in range(NR):
        v += ([amp_min,amp_max,1,0,0,0.5,0,0] if r==16 else [0.0,1.0,0,0,0,0.5,0,0])
    v += [0.1,0.5,1.0,0.0,1.0,0.5,0.0,0.0,1.0,0.5, 0.05,0.0,0.0,0.0,0.0,0.0, 0.0,0.0,
          float(semitone),440.0,0.0, float(moog_cutoff),0.0,0.0, 800.0,0.7,12.0,0.0, 0.0,0.0,0.5,0.0]
    v += [0,0,0,0,0, 4,4,4, 4,4,4, 4,4,4, 0,4, 4,0,1,2, 60,-1, 1,1,0,69,-1,2, 0,0]
    v += [0]+[0.0]*128
    v += [0]+[0.0]*128
    return v
def fmt(x): return ("%d"%x) if isinstance(x,int) else ("%.9g"%x)
lines=["ligase_morph 1","power 2","cursor 0 0"]
for sid,(semi,moog,amn,amx) in {0:(0,300,0.1,0.4),1:(12,6000,0.5,0.9),2:(-12,1000,0.3,0.6)}.items():
    lines.append("snap %d "%sid + " ".join(fmt(x) for x in snap_values(semi,moog,amn,amx)))
lines += ["point 0 0 0","point 1 1 0","point 2 0 1","route 1 0 2 0"]
open("imagined.txt","w").write("\n".join(lines)+"\n")
print("wrote imagined.txt")

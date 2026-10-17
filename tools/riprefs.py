import sys,struct
d=open(sys.argv[1],'rb').read()
text_off=0x400; text_va=0x140001000; text_len=0x26dc00-0x400
targets={int(x,16) for x in sys.argv[2:]}
t=d[text_off:text_off+text_len]
for i in range(len(t)-7):
    # 48/4c 8d modrm(rip) disp32
    if t[i] in (0x48,0x4c) and t[i+1]==0x8d and (t[i+2]&0xc7)==0x05:
        disp=struct.unpack_from('<i',t,i+3)[0]
        va=text_va+i+7+disp
        if va in targets: print(hex(text_va+i),'->',hex(va))

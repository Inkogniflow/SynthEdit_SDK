# Rasterises the drawing calls recorded from the real WavetableDisplayGui::OnRender.
import sys
from PIL import Image, ImageDraw, ImageFont
SS=3
def srgb(c):  # GmpiDrawing colours are linear
    c=max(0.0,min(1.0,c)); return int(round(255*(12.92*c if c<=0.0031308 else 1.055*c**(1/2.4)-0.055)))
def load(path):
    lines=open(path).read().split('\n'); W,H=map(float,lines[0].split()); i=1; ops=[]
    while i<len(lines) and lines[i].strip():
        k,r,g,b,a,w,n=lines[i].split(); i+=1; figs=[]
        for _ in range(int(n)):
            v=list(map(float,lines[i].split()))[1:]; i+=1; figs.append(list(zip(v[0::2],v[1::2])))
        ops.append((k,(srgb(float(r)),srgb(float(g)),srgb(float(b)),int(255*float(a))),float(w),figs))
    return int(W),int(H),ops
def render(path):
    W,H,ops=load(path); img=Image.new('RGBA',(W*SS,H*SS),(0,0,0,255))
    for k,col,w,figs in ops:
        L=Image.new('RGBA',img.size,(0,0,0,0)); d=ImageDraw.Draw(L)
        for f in figs:
            pts=[(x*SS,y*SS) for x,y in f]
            if k=='fill': d.polygon(pts,fill=col)
            else: d.line(pts,fill=col,width=max(1,int(round(w*SS))),joint='curve')
        img.alpha_composite(L)
    return img.resize((W,H),Image.LANCZOS).convert('RGB')
a,b=render('gui3d.ops'),render('gui2d.ops')
pad=14; lab=22
out=Image.new('RGB',(a.width+b.width+3*pad,a.height+lab+2*pad),(245,245,245))
try: font=ImageFont.truetype('DejaVuSans.ttf',13)
except: font=ImageFont.load_default()
d=ImageDraw.Draw(out)
out.paste(a,(pad,pad)); d.text((pad,pad+a.height+5),'View 3D, Standardfarben',fill=(40,40,40),font=font)
out.paste(b,(2*pad+a.width,pad)); d.text((2*pad+a.width,pad+b.height+5),'View 2D, Wave Color = #FFB347',fill=(40,40,40),font=font)
out.save(sys.argv[1]); print('saved')

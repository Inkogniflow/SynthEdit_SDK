# Rasterises the drawing calls recorded from the real WaveshaperDisplayGui::OnRender (run_tests.sh writes the .ops files).
# usage: python3 render_ops.py out.png [file.ops "label" ...]
import sys
from PIL import Image, ImageDraw, ImageFont
SS=3
def srgb(c):  # GmpiDrawing colours are linear
    c=max(0.0,min(1.0,c)); return int(round(255*(12.92*c if c<=0.0031308 else 1.055*c**(1/2.4)-0.055)))
def font(px):
    for f in ('DejaVuSans.ttf','/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf','Arial.ttf'):
        try: return ImageFont.truetype(f,int(px))
        except Exception: pass
    return ImageFont.load_default()
def load(path):
    lines=open(path).read().split('\n'); W,H=map(float,lines[0].split()); i=1; ops=[]
    while i<len(lines) and lines[i].strip():
        parts=lines[i].split(); i+=1
        if parts[0]=='text':
            r,g,b,a,size,al,pal,x0,y0,x1,y1=map(float,parts[1:]); txt=lines[i]; i+=1
            ops.append(('text',(srgb(r),srgb(g),srgb(b),int(255*a)),(size,int(al),int(pal),x0,y0,x1,y1,txt)))
            continue
        k,r,g,b,a,w,n=parts; figs=[]
        for _ in range(int(n)):
            v=list(map(float,lines[i].split()))[1:]; i+=1; figs.append(list(zip(v[0::2],v[1::2])))
        ops.append((k,(srgb(float(r)),srgb(float(g)),srgb(float(b)),int(255*float(a))),(float(w),figs)))
    return int(W),int(H),ops
def render(path):
    W,H,ops=load(path); img=Image.new('RGBA',(W*SS,H*SS),(0,0,0,255))
    for k,col,data in ops:
        L=Image.new('RGBA',img.size,(0,0,0,0)); d=ImageDraw.Draw(L)
        if k=='text':
            size,al,pal,x0,y0,x1,y1,txt=data; f=font(size*SS)
            tw=d.textlength(txt,font=f); asc,desc=f.getmetrics()
            x={0:x0*SS,1:x1*SS-tw,2:(x0+x1)*SS/2-tw/2}[al]
            y={0:y0*SS,1:y1*SS-asc-desc,2:(y0+y1)*SS/2-(asc+desc)/2}[pal]
            Lc=Image.new('RGBA',img.size,(0,0,0,0)); dc=ImageDraw.Draw(Lc); dc.text((x,y),txt,font=f,fill=col)
            mask=Image.new('L',img.size,0); ImageDraw.Draw(mask).rectangle([x0*SS,y0*SS,x1*SS,y1*SS],fill=255)  # clip
            L.paste(Lc,(0,0),Image.composite(Lc.getchannel('A'),Image.new('L',img.size,0),mask))
        else:
            w,figs=data
            for fg in figs:
                pts=[(x*SS,y*SS) for x,y in fg]
                if len(pts)<2: continue
                if k=='fill': d.polygon(pts,fill=col)
                else: d.line(pts,fill=col,width=max(1,int(round(w*SS))),joint='curve')
        img.alpha_composite(L)
    return img.resize((W,H),Image.LANCZOS).convert('RGB')
args=sys.argv[2:] or ['gui2d.ops','View 2D','gui3d.ops','View 3D, Curve Color #5BE7FF','gui_error.ops','Formelfehler']
imgs=[(render(args[i]),args[i+1]) for i in range(0,len(args),2)]
pad=14; lab=22; scale=2
imgs=[(im.resize((im.width*scale,im.height*scale),Image.LANCZOS),l) for im,l in imgs]
W=sum(im.width for im,_ in imgs)+pad*(len(imgs)+1); H=max(im.height for im,_ in imgs)+lab+2*pad
out=Image.new('RGB',(W,H),(245,245,245)); d=ImageDraw.Draw(out); f=font(15); x=pad
for im,l in imgs:
    out.paste(im,(x,pad)); d.text((x,pad+im.height+5),l,fill=(40,40,40),font=f); x+=im.width+pad
out.save(sys.argv[1]); print('saved',out.size)

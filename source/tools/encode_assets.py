"""Prepare generated reference artwork and baked text raster for RGB565 SDK display.
Requires Pillow at asset-generation time only. No runtime image/font library.
"""
from pathlib import Path
from PIL import Image,ImageFont,ImageDraw
import argparse
ap=argparse.ArgumentParser();ap.add_argument('--font',default=str(Path(__file__).resolve().parents[1]/'assets/Geist-Medium.ttf'));a=ap.parse_args()
p=Path(__file__).resolve().parents[1]
im=Image.open(p/'assets/gravity-reference.png').convert('RGB').resize((320,172),Image.Resampling.LANCZOS)
q=im.quantize(colors=256);pa=q.getpalette()
def arr(name,ty,v):return 'static const '+ty+' '+name+'[] = {\n'+''.join(','.join(map(str,v[i:i+32]))+',\n' for i in range(0,len(v),32))+'};\n'
out='// RGB565 indexed artwork and baked raster text; no runtime font dependency.\n#include <stdint.h>\n'
out+=arr('art_palette','uint16_t',[((pa[3*i]>>3)<<11)|((pa[3*i+1]>>2)<<5)|(pa[3*i+2]>>3) for i in range(256)])
out+=arr('art_pixels','uint8_t',list(q.tobytes()))
chars=' ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:#%/-'
for sz in [9,13,42]:
 f=ImageFont.truetype(a.font,sz);offs=[0];widths=[];data=[]
 for ch in chars:
  width=int(f.getlength(ch)+1);height=sz+4
  tile=Image.new('L',(width,height));ImageDraw.Draw(tile).text((0,-int(sz*.18)),ch,font=f,fill=255)
  data+=list(tile.tobytes());offs.append(len(data));widths.append(width)
 out+=f'static const char chars{sz}[]="{chars}";\n'
 out+=arr('glyph_offsets'+str(sz),'uint32_t',offs)+arr('glyph_widths'+str(sz),'uint8_t',widths)+arr('glyph_alpha'+str(sz),'uint8_t',data)
(p/'src/art.h').write_text(out)
print('Encoded image: 55,552B indexed RGB565. Source',len(out),'bytes')

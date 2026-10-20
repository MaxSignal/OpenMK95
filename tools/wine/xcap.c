// capture an 8-bit X root window through a given colormap -> PPM
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
  Display*d=XOpenDisplay(NULL); if(!d) return 1;
  Window root=DefaultRootWindow(d);
  Colormap cm=argc>2? strtoul(argv[2],0,0): DefaultColormap(d,0);
  XWindowAttributes wa; XGetWindowAttributes(d,root,&wa);
  int x=0,y=0,w=wa.width,h=wa.height;
  if(argc>6){x=atoi(argv[3]);y=atoi(argv[4]);w=atoi(argv[5]);h=atoi(argv[6]);}
  XImage*im=XGetImage(d,root,x,y,w,h,AllPlanes,ZPixmap);
  XColor c[256]; for(int i=0;i<256;i++){c[i].pixel=i;} XQueryColors(d,cm,c,256);
  FILE*f=fopen(argv[1],"wb"); fprintf(f,"P6\n%d %d\n255\n",w,h);
  for(int j=0;j<h;j++)for(int i=0;i<w;i++){unsigned long p=XGetPixel(im,i,j)&255; unsigned char rgb[3]={c[p].red>>8,c[p].green>>8,c[p].blue>>8}; fwrite(rgb,1,3,f);}
  fclose(f); return 0;}

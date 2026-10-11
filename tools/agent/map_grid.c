#include <stdio.h>
#include <stdlib.h>
#include "cs2d_map.h"
static cs2d_map_t m;
int main(int argc,char**argv){ cs2d_map_load_file(&m,argv[1]); int x0=atoi(argv[2]),y0=atoi(argv[3]),r=atoi(argv[4]);
 for(int y=y0-r;y<=y0+r;y++){ printf("%3d ",y); for(int x=x0-r;x<=x0+r;x++){ int c=m.collision[y*m.width+x]; char ch=".WO#"[c>3?3:c];
  if(argc>5) ch="0123456789abcdefghijklmnopqrstuvwxyz"[m.tile_modes[m.tiles[y*m.width+x]]];
  printf("%c",ch);} printf("\n"); } printf("    x from %d\n",x0-r); }

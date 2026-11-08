#!/usr/bin/env python3
"""Audit host text placement in a temporary instrumented source copy.

No firmware edits or shared build writes. Requires existing host DSP objects;
--strict returns failure for any reported horizontal clip. Vertical strip
clipping is intentional and is not measured; visual review remains necessary.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from run_screen_transitions import FLAGS

TRACE = r'''
static uint32_t font_audit_count, font_collision_count;
static int font_audit_silent;
static int font_header, font_param_prev;
static char font_audit_seen[1024][160];
static void font_audit(int32_t x, int32_t y, const char *text, int scale) {
    if (!text || !*text || y+cv_oy >= (int32_t)cv_h || y+cv_oy+8*scale <= 0) return;
    int32_t w=br_widget_text_width(text);
    if(scale==2 && w) w=(w+1)*2-2;
    if(font_header && scale==2 && x+w>190) {
        font_collision_count++;
        if(!font_audit_silent) printf("COLLISION header x=%d end=%d title=%s\n",x,x+w,text);
    }
    if(x>=0 && x+w<=(int32_t)cv_w) return;
    char key[160]; snprintf(key,sizeof key,"canvas=%u x=%d width=%d scale=%d text=%.80s",cv_w,x,w,scale,text);
    for(uint32_t i=0;i<font_audit_count && i<1024u;i++)if(!strcmp(font_audit_seen[i],key))return;
    if(font_audit_count<1024u)snprintf(font_audit_seen[font_audit_count],160,"%s",key);
    font_audit_count++;
    if(!font_audit_silent) printf("CLIPPED part=%u page=%u screen=%u %s\n",br_ui.part,br_ui.page,br_ui.screen[br_ui.page],key);
}
static void font_audit_param_row(int center, int y, const char *line) {
    int width=br_widget_text_width(line), x=center-width/2;
    if(x<center-30 || x+width>center+30 || (font_param_prev>=0 && y<font_param_prev+8) || y+8>134) {
        font_collision_count++;
        if(!font_audit_silent) printf("COLLISION param part=%u page=%u screen=%u x=%d y=%d width=%d previous=%d text=%s\n",br_ui.part,br_ui.page,br_ui.screen[br_ui.page],x,y,width,font_param_prev,line);
    }
    font_param_prev=y;
}
static void font_audit_badge(int label_y, int badge_y, uint32_t badges) {
    if(badges && label_y+8>badge_y) {
        font_collision_count++;
        if(!font_audit_silent) printf("COLLISION badge part=%u page=%u screen=%u label=%d badge=%d\n",br_ui.part,br_ui.page,br_ui.screen[br_ui.page],label_y,badge_y);
    }
}
'''

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--strict', action='store_true')
    args=ap.parse_args()
    objects=sorted((ROOT/'build/host/braids').glob('*.o'))+sorted((ROOT/'build/host/plaits_reference').glob('*.o'))
    if not objects: raise SystemExit('Host DSP objects missing; build them separately before this audit')
    with tempfile.TemporaryDirectory(prefix='fm1-font-fit-') as tmp:
        d=Path(tmp); (d/'firmware').mkdir(); (d/'tests').mkdir()
        shutil.copytree(ROOT/'firmware/src', d/'firmware/src')
        for name in ('hal','gen'):
            (d/'firmware'/name).symlink_to(ROOT/'firmware'/name, target_is_directory=True)
        shutil.copy2(ROOT/'tests/test_font_fit.c',d/'tests/test_font_fit.c')
        shutil.copy2(ROOT/'tests/project_device_host.c',d/'tests/project_device_host.c')
        path=d/'firmware/src/instrument_widgets.c'; source=path.read_text()
        marker='static void br_widget_text(int32_t x, int32_t y, const char *text, uint16_t color)\n{'
        marker2='static void br_widget_text_2x(int32_t x, int32_t y, const char *text, uint16_t color)\n{'
        if source.count(marker)!=1 or source.count(marker2)!=1:
            raise SystemExit('Text renderer changed; update audit instrumentation before relying on it')
        source=source.replace(marker, TRACE+'\n'+marker+'\n    font_audit(x,y,text,1);')
        source=source.replace(marker2, marker2+'\n    font_audit(x,y,text,2);')
        path.write_text(source)
        path=d/'firmware/src/instrument_ui.c'; source=path.read_text()
        replacements={
          '        line[n] = 0;\n        br_widget_text(center': '        line[n] = 0;\n        font_audit_param_row(center,y,line);\n        br_widget_text(center',
          '    int32_t y = br_param_rows(value, center, value_y, C_WHITE);': '    font_param_prev=-1;\n    int32_t y = br_param_rows(value, center, value_y, C_WHITE);',
          '        br_draw_route_badges(30, badge_y, route_badges, br_ui.part, BR_BACKGROUND);': '        font_audit_badge(label_y,badge_y,route_badges);\n        br_draw_route_badges(30, badge_y, route_badges, br_ui.part, BR_BACKGROUND);',
          'static void br_header_draw_title(const char *name)\n{': 'static void br_header_draw_title(const char *name)\n{\n    font_header=1;',
          '\n}\nstatic void br_track_badge(int32_t x': '\n    font_header=0;\n}\nstatic void br_track_badge(int32_t x',
        }
        for old,new in replacements.items():
            if source.count(old)!=1: raise SystemExit('UI layout changed; update audit instrumentation before relying on it: '+old[:60])
            source=source.replace(old,new)
        path.write_text(source)
        flags=[f.replace('-Ifirmware/',f'-I{d}/firmware/') for f in FLAGS]
        planner=d/'planner.o'; binary=d/'audit'
        subprocess.run(['cc',*flags,'-c',str(d/'firmware/src/audio_fixed8_plan_unit.c'),'-o',str(planner)],cwd=ROOT,check=True)
        subprocess.run(['c++','-x','c',*flags,str(d/'tests/test_font_fit.c'),'-x','none',str(planner),*map(str,objects),'-o',str(binary)],cwd=ROOT,check=True)
        result=subprocess.run([str(binary)],cwd=ROOT,capture_output=True,text=True,check=True)
        print(result.stdout,end='')
        if args.strict and ('CLIPPED ' in result.stdout or 'COLLISION ' in result.stdout): return 1
    return 0
if __name__=='__main__': raise SystemExit(main())

#!/usr/bin/env python3
from __future__ import annotations
import csv, uuid
from pathlib import Path
from typing import NamedTuple

ROOT = Path(__file__).resolve().parents[2]
WIRING = ROOT / 'hardware' / 'wiring.csv'
OUT = ROOT / 'hardware' / 'kicad' / 'MF35X_Tracker.kicad_sch'
PROJECT = 'MF35X_Tracker'
ROOT_UUID = '7c44dcb5-b2e5-41b8-9fe6-5d37a9a1d001'
NS = uuid.UUID(ROOT_UUID)

def uid(key:str)->str: return str(uuid.uuid5(NS,key))
class Pin(NamedTuple):
    number:str; name:str; direction:str; x:float; y:float; angle:float
class Symbol(NamedTuple):
    lib_id:str; ref:str; value:str; x:float; y:float; pins:list[Pin]

def vp(items,left=True,spacing=2.54,x=8.0):
    n=len(items); ys=[(n-1)*spacing/2-i*spacing for i in range(n)]
    xx=-x if left else x; a=180.0 if left else 0.0
    return [Pin(num,name,d,xx,y,a) for (num,name,d),y in zip(items,ys)]
def two(left,right): return vp(left,True)+vp(right,False)
def r_pins(): return [Pin('1','1','passive',0,-3.81,90),Pin('2','2','passive',0,3.81,270)]

ESP=two([
('1','5V_IN','power_in'),('2','3V3','power_out'),('3','GND','power_in'),('4','GPIO8','bidirectional'),('5','GPIO9','bidirectional'),('6','GPIO10','input'),('7','GPIO0','passive')],
[('8','GPIO11','output'),('9','GPIO12','output'),('10','GPIO13','output'),('11','GPIO14','input'),('12','GPIO16','input'),('13','GPIO17','output')])
ADS=two([('1','VDD','power_in'),('2','GND','power_in'),('3','SCL','input'),('4','SDA','bidirectional'),('5','ADDR','input')],
[('6','AIN0','input'),('7','AIN1','input'),('8','AIN2','input'),('9','AIN3','input')])
GPS=vp([('1','VCC','power_in'),('2','GND','power_in'),('3','TX','output'),('4','RX','input')],True)
MAX=vp([('1','VCC','power_in'),('2','GND','power_in'),('3','CLK','input'),('4','CS','input'),('5','DO','output'),('6','T+','passive'),('7','T-','passive')],True)
HY=two([('1','J2_OUT','passive'),('2','J2_GND','passive')],[('3','J1_VCC','power_in'),('4','J1_GND','power_in'),('5','J1_OUT','output')])
CONN2=vp([('1','PIN1','passive'),('2','PIN2','passive')],True,x=8.0)
SENSOR2=vp([('1','SENSE','passive'),('2','RETURN_GND','passive')],True,x=8.0)
THERMO2=vp([('1','T+','passive'),('2','T-','passive')],True,x=8.0)

COMPONENTS=[
Symbol('MF35X:ESP32_S3_USED','U1','Freenove ESP32-S3 WROOM Lite FNK0099A',50,55,ESP),
Symbol('MF35X:ADS1115','U2','ADS1115 @ 0x48',112,55,ADS),
Symbol('MF35X:GPS_ATGM336H','U3','ATGM336H GPS / UART 115200 / 10 Hz',170,55,GPS),
Symbol('MF35X:MAX31855','U4','MAX31855 K-type',170,95,MAX),
Symbol('MF35X:HY_M154','U5','HY-M154 optocoupler / RPM W-terminal',112,100,HY),
Symbol('Connector_Generic:Conn_01x02','J1','To external solenoid-valve driver: CTRL / return',50,100,CONN2),
Symbol('Connector_Generic:Conn_01x02','J2','5 V DC/DC input',50,130,CONN2),
Symbol('Device:R','R1','980R oil-temperature divider',85,145,r_pins()),
Symbol('MF35X:SENSOR_CASE','J3','VDO 801/1/6 oil-temperature sender',85,172,SENSOR2),
Symbol('Device:R','R2','216R measured (firmware main calc still 220R)',125,145,r_pins()),
Symbol('MF35X:SENSOR2','J4','Oil-pressure sender',125,172,SENSOR2),
Symbol('Connector_Generic:Conn_01x02','J5','Vehicle battery sense + / GND',50,172,CONN2),
Symbol('Device:R','R3','100k battery divider upper',155,150,r_pins()),
Symbol('Device:R','R4','10k battery divider lower',155,178,r_pins()),
Symbol('Connector_Generic:Conn_01x02','J6','K-type thermocouple T+ / T-',195,130,THERMO2),
]

def esc(s): return s.replace('\\','\\\\').replace('"','\\"')

def lib_graphics(lib_id,pins):
    short=lib_id.split(':',1)[1]
    maxy=max([abs(p.y) for p in pins] or [2.54])
    body=[]
    # Keep symbol graphics to syntax already validated by KiCad in DRAFT-1.
    # Electrical meaning comes from pin definitions and net labels; rectangles
    # provide the component/connector body without unsupported graphic tokens.
    if lib_id=='Device:R':
        body.append('        (rectangle (start -1.30 -2.20) (end 1.30 2.20) (stroke (width 0.254) (type default)) (fill (type background)))')
    elif 'Conn_01x02' in lib_id:
        body.append('        (rectangle (start -2.20 -2.20) (end 2.20 2.20) (stroke (width 0.254) (type default)) (fill (type background)))')
    elif short=='SENSOR_CASE':
        body.append('        (rectangle (start -3.20 -3.20) (end 3.20 3.20) (stroke (width 0.254) (type default)) (fill (type background)))')
    else:
        body.append(f'        (rectangle (start -5.50 {-maxy-1.27:.2f}) (end 5.50 {maxy+1.27:.2f}) (stroke (width 0.254) (type default)) (fill (type background)))')
    return '\n'.join(body)

def lib_symbol_block(lib_id,pins):
    short=lib_id.split(':',1)[1]
    maxy=max([abs(p.y) for p in pins] or [2.54]); y1=-maxy-1.27; y2=maxy+1.27
    pin_defs=''.join(
        f'      (pin {p.direction} line (at {p.x:.2f} {p.y:.2f} {p.angle:.0f}) (length 2.54)\n'
        f'        (name "{esc(p.name)}" (effects (font (size 1.0 1.0))))\n'
        f'        (number "{esc(p.number)}" (effects (font (size 1.0 1.0))))\n      )\n' for p in pins)
    refy=5.8 if lib_id=='Device:R' else y2+2
    valy=-5.8 if lib_id=='Device:R' else y1-2
    return f'''    (symbol "{esc(lib_id)}" (pin_names (offset 0.5)) (in_bom yes) (on_board yes)
      (property "Reference" "X" (at 0 {refy:.2f} 0) (effects (font (size 1.27 1.27))))
      (property "Value" "{esc(short)}" (at 0 {valy:.2f} 0) (effects (font (size 1.27 1.27))))
      (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
      (property "Datasheet" "~" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
      (symbol "{esc(short)}_0_1"
{lib_graphics(lib_id,pins)}
{pin_defs}      )
    )'''

def inst(sym):
    pinb=''.join(f'    (pin "{esc(p.number)}" (uuid {uid(f"pin:{sym.ref}:{p.number}")}))\n' for p in sym.pins)
    return f'''  (symbol (lib_id "{esc(sym.lib_id)}") (at {sym.x:.2f} {sym.y:.2f} 0) (unit 1)
    (in_bom yes) (on_board yes) (uuid {uid(f"symbol:{sym.ref}")})
    (property "Reference" "{esc(sym.ref)}" (at {sym.x+7:.2f} {sym.y-3:.2f} 0) (effects (font (size 1.27 1.27))))
    (property "Value" "{esc(sym.value)}" (at {sym.x+7:.2f} {sym.y:.2f} 0) (effects (font (size 1.0 1.0))))
    (property "Footprint" "" (at {sym.x:.2f} {sym.y:.2f} 0) (effects (font (size 1.27 1.27)) hide))
    (property "Datasheet" "~" (at {sym.x:.2f} {sym.y:.2f} 0) (effects (font (size 1.27 1.27)) hide))
{pinb}    (instances (project "{PROJECT}" (path "/{ROOT_UUID}" (reference "{sym.ref}") (unit 1))))
  )'''

def gl(net,x,y,key):
    return f'''  (global_label "{esc(net)}" (shape bidirectional) (at {x:.2f} {y:.2f} 0)
    (effects (font (size 0.85 0.85)) (justify left))
    (uuid {uid(f"label:{key}:{net}")})
  )'''
def nc(x,y,key): return f'  (no_connect (at {x:.2f} {y:.2f}) (uuid {uid(f"nc:{key}")}))'
def txt(s,x,y,size=1.0,b=False):
    weight=' bold' if b else ''
    return f'''  (text "{esc(s)}" (at {x:.2f} {y:.2f} 0)
    (effects (font (size {size:.2f} {size:.2f}){weight}) (justify left bottom))
    (uuid {uid(f"text:{x}:{y}:{s}")})
  )'''

def pin_lookup():
    out={}
    for s in COMPONENTS:
        for p in s.pins:
            out[f'{s.ref}.{p.name}']=(s,p); out[f'{s.ref}.{p.number}']=(s,p)
    return out

def load_wiring():
    with WIRING.open(newline='',encoding='utf-8') as h: return list(csv.DictReader(h))

def build():
    rows=load_wiring(); lookup=pin_lookup(); libs={}
    for s in COMPONENTS: libs.setdefault(s.lib_id,s.pins)
    labels=set(); ncs=set(); todos=[]
    for row in rows:
        net=row['net'].strip(); status=row['status'].strip()
        if status=='TODO': todos.append(f"{net}: {row['notes'].strip()}")
        for ep in (row['endpoint_a'].strip(),row['endpoint_b'].strip()):
            if ep not in lookup: continue
            s,p=lookup[ep]; key=f'{s.ref}.{p.name}'; x=s.x+p.x; y=s.y-p.y
            if status in {'NC_RESERVED','NC_UNUSED'}: ncs.add((key,x,y))
            else: labels.add((key,net,x,y))
    out=['(kicad_sch','  (version 20231120)','  (generator "chatgpt_mf35x_hw_final")',f'  (uuid {ROOT_UUID})','  (paper "A4")',
         '  (title_block','    (title "MF35X Livetracker - Trackerbox Electrical Schematic")','    (date "2026-09-10")','    (rev "HW-REV1")','    (company "MF35X Tracker")','    (comment 1 "Confirmed tracker wiring; external solenoid power driver remains boundary-defined")','  )','  (lib_symbols']
    out.extend(lib_symbol_block(k,v) for k,v in libs.items()); out.append('  )')
    out.extend(inst(s) for s in COMPONENTS)
    for key,net,x,y in sorted(labels): out.append(gl(net,x,y,key))
    for key,x,y in sorted(ncs): out.append(nc(x,y,key))
    out.append(txt('MF35X LIVETRACKER - TRACKERBOX / HW-REV1',20,15,1.6,True))
    out.append(txt('POWER + CORE',20,21,1.0,True)); out.append(txt('SENSORS / I-O',20,138,1.0,True))
    out.append(txt('GPIO0 is intentionally NC/reserved. ADS1115 AIN3 is intentionally unused.',20,194,0.9,True))
    out.append(txt('HY-M154 verified: W -> J2-OUT; J2-GND -> GND; J1-VCC -> +5V; J1-GND -> GND; J1-OUT -> GPIO10.',20,198,0.82,True))
    out.append(txt('GPIO11 is the solenoid-valve control signal. The valve coil is NOT powered directly from the ESP32 pin.',20,202,0.82,True))
    if todos:
        out.append(txt('OPEN PHYSICAL DETAIL:',140,194,0.85,True))
        y=198
        for item in todos[:3]: out.append(txt(item,140,y,0.62,False)); y+=2.7
    out.extend(['  (sheet_instances','    (path "/" (page "1"))','  )',')'])
    return '\n'.join(out)+'\n'

if __name__=='__main__':
    OUT.write_text(build(),encoding='utf-8'); print(OUT,OUT.stat().st_size)

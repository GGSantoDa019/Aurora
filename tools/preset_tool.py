"""Offline AuroraState/AuroraJson/XferJson JSON exporter and AuroraState writer.
Python 3.14+, standard library only. Never executes a plugin or opens external assets.
CBOR reader adapted from the user-owned static investigation's offline inspector.
"""
from pathlib import Path
from compression import zstd
import argparse, base64, json, math, struct, sys, xml.etree.ElementTree as ET
LIMIT=128*1024*1024
BREAK=object()
class CBORReader:
    def __init__(self,data): self.data=data; self.pos=0; self.items=0
    def take(self,n):
        if n<0 or n>len(self.data)-self.pos: raise ValueError('Truncated CBOR')
        b=self.data[self.pos:self.pos+n];self.pos+=n;return b
    def length(self,ai):
        if ai<24:return ai
        sizes={24:1,25:2,26:4,27:8}
        if ai==31:return None
        if ai not in sizes:raise ValueError('Reserved CBOR additional information')
        return int.from_bytes(self.take(sizes[ai]),'big')
    def read(self,depth=0,allow_break=False):
        self.items+=1
        if depth>96 or self.items>2000000:raise ValueError('CBOR depth/item limit')
        token=self.take(1)[0];major=token>>5;ai=token&31
        if major==7:
            if ai==31:
                if allow_break:return BREAK
                raise ValueError('Unexpected CBOR break')
            if ai in (20,21):return ai==21
            if ai==22:return None
            if ai==23:return {'$undefined':True}
            if ai in (25,26,27):
                value=struct.unpack({25:'>e',26:'>f',27:'>d'}[ai],self.take({25:2,26:4,27:8}[ai]))[0]
                return value if math.isfinite(value) else {'$float':str(value)}
            if ai>=28:raise ValueError('Reserved CBOR simple/float encoding')
            return {'$simple':self.take(1)[0] if ai==24 else ai}
        n=self.length(ai)
        if major in (0,1):
            if n is None:raise ValueError('Indefinite integer')
            return n if major==0 else -1-n
        if major in (2,3):
            if n is None:
                chunks=[]
                while True:
                    if self.pos>=len(self.data):raise ValueError('Unterminated string')
                    if self.data[self.pos]==255:self.pos+=1;break
                    head=self.take(1)[0]
                    if head>>5!=major or head&31==31:raise ValueError('Invalid indefinite string chunk')
                    chunks.append(self.take(self.length(head&31)))
                raw=b''.join(chunks)
            else:raw=self.take(n)
            return raw.decode('utf-8') if major==3 else {'$bytes_base64':base64.b64encode(raw).decode('ascii')}
        if major==4:
            values=[]
            if n is None:
                while True:
                    x=self.read(depth+1,True)
                    if x is BREAK:break
                    values.append(x)
            else:
                if n>2000000:raise ValueError('Array size limit')
                values=[self.read(depth+1) for _ in range(n)]
            return values
        if major==5:
            pairs=[]
            if n is None:
                while True:
                    key=self.read(depth+1,True)
                    if key is BREAK:break
                    pairs.append([key,self.read(depth+1)])
            else:
                if n>1000000:raise ValueError('Map size limit')
                pairs=[[self.read(depth+1),self.read(depth+1)] for _ in range(n)]
            if all(isinstance(k,str) for k,v in pairs) and len({k for k,v in pairs})==len(pairs):return dict(pairs)
            return {'$map':pairs}
        if major==6:
            if n is None:raise ValueError('Indefinite tag')
            return {'$tag':n,'value':self.read(depth+1)}
        raise ValueError('Unsupported CBOR major type')

def head(major,n):
    if n<24:return bytes([(major<<5)|n])
    for ai,fmt,limit in [(24,'>B',255),(25,'>H',65535),(26,'>I',0xffffffff),(27,'>Q',0xffffffffffffffff)]:
        if n<=limit:return bytes([(major<<5)|ai])+struct.pack(fmt,n)
    raise ValueError('CBOR integer overflow')
def cbor(v,depth=0):
    if depth>64:raise ValueError('CBOR depth limit')
    if v is None:return b'\xf6'
    if type(v) is bool:return b'\xf5' if v else b'\xf4'
    if type(v) is int:return head(0,v) if v>=0 else head(1,-1-v)
    if type(v) is float:
        if not math.isfinite(v):raise ValueError('Non-finite number')
        return b'\xfb'+struct.pack('>d',v)
    if type(v) is str:
        raw=v.encode('utf-8');return head(3,len(raw))+raw
    if type(v) is list:return head(4,len(v))+b''.join(cbor(x,depth+1) for x in v)
    if type(v) is dict:
        if set(v)=={'$bytes_base64'}:
            raw=base64.b64decode(v['$bytes_base64'],validate=True);return head(2,len(raw))+raw
        if set(v)=={'$tag','value'}:return head(6,v['$tag'])+cbor(v['value'],depth+1)
        return head(5,len(v))+b''.join(cbor(k,depth+1)+cbor(x,depth+1) for k,x in v.items())
    raise ValueError('Unsupported JSON/CBOR value')
def tree(element):
    return {'type':element.tag,'properties':dict(element.attrib),'children':[tree(c) for c in element]}
def unpack(blob):
    if not 0<len(blob)<=LIMIT:raise ValueError('Invalid file length')
    if blob.startswith(b'AuroraState\0'):marker=b'AuroraState\0';pos=12;meta,=struct.unpack_from('<Q',blob,pos);pos+=8;kind='AuroraState'
    elif blob.startswith(b'XferJson\0'):marker=b'XferJson\0';pos=9;meta,=struct.unpack_from('<Q',blob,pos);pos+=8;kind='XferJson'
    elif blob.startswith(b'AuroraJson\0'):marker=b'AuroraJson\0';pos=11;meta,=struct.unpack_from('<I',blob,pos);pos+=4;kind='AuroraJson'
    else:raise ValueError('Unknown marker')
    if meta>8*1024*1024 or pos+meta+8>len(blob):raise ValueError('Invalid metadata length')
    metadata=json.loads(blob[pos:pos+meta].decode('utf-8'));pos+=meta
    if kind=='AuroraJson':
        plain,selector,packed=struct.unpack_from('<IBI',blob,pos);pos+=9
        if packed!=len(blob)-pos:raise ValueError('Packed length mismatch')
    else:plain,selector=struct.unpack_from('<II',blob,pos);pos+=8
    if selector!=2 or not 0<plain<=LIMIT:raise ValueError('Unsupported selector or payload size')
    decoder=zstd.ZstdDecompressor();payload=decoder.decompress(blob[pos:],max_length=plain+1)
    if len(payload)!=plain or not decoder.eof or decoder.unused_data:raise ValueError('Zstd frame size/trailing-data mismatch')
    reader=CBORReader(payload);state=reader.read()
    if reader.pos!=len(payload):raise ValueError('Trailing CBOR data')
    return {'format':kind,'metadata':metadata,'state':state}
def pack(document):
    metadata=json.dumps(document['metadata'],ensure_ascii=False,separators=(',',':')).encode('utf-8')
    state=document['state']
    if document.get('format')=='AuroraJson':
        binary=base64.b64decode(state['state']['$bytes_base64'],validate=True)
        size,=struct.unpack_from('<I',binary,4)
        state={'schema':2,'tree':tree(ET.fromstring(binary[8:8+size]))}
    if state.get('schema')==1:state=dict(state,schema=2)
    if state.get('schema')!=2 or 'tree' not in state:raise ValueError('Only Aurora structured schemas 1 and 2 can be written')
    payload=cbor(state)
    if len(payload)>LIMIT or len(metadata)>8*1024*1024:raise ValueError('Document size limit')
    return b'AuroraState\0'+struct.pack('<Q',len(metadata))+metadata+struct.pack('<II',len(payload),2)+zstd.compress(payload,level=3)
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('operation',choices=['inspect','pack']);parser.add_argument('input',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args()
    if args.operation=='inspect':args.output.write_text(json.dumps(unpack(args.input.read_bytes()),ensure_ascii=False,indent=2,allow_nan=False),encoding='utf-8')
    else:args.output.write_bytes(pack(json.loads(args.input.read_text(encoding='utf-8'))))
if __name__=='__main__':
    try:main()
    except (ValueError,KeyError,IndexError,struct.error,ET.ParseError,zstd.ZstdError) as error:sys.exit('Invalid preset: '+str(error))

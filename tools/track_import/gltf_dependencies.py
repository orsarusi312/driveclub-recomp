# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate local glTF/GLB buffer and image dependencies before Blender import."""
import base64
import hashlib
import json
from pathlib import Path
import struct
from urllib.parse import unquote, urlsplit
from track_import import require, mapping, objects


def inspect(path):
    path=Path(path).resolve(); data=path.read_bytes(); binary_size=0
    if path.suffix.lower()=='.glb':
        require(len(data)>=20,'truncated GLB')
        magic,version,size=struct.unpack_from('<III',data)
        require(magic==0x46546c67 and version==2 and size==len(data),'invalid GLB header')
        at=12; chunks=[]
        while at<len(data):
            require(at+8<=len(data),'truncated GLB chunk')
            length,kind=struct.unpack_from('<II',data,at); at+=8
            require(length%4==0 and at+length<=len(data),'invalid GLB chunk')
            chunks.append((kind,data[at:at+length])); at+=length
        require([kind for kind,_ in chunks] in ([0x4e4f534a],[0x4e4f534a,0x004e4942]),'unsupported GLB chunks')
        root=mapping(json.loads(chunks[0][1]),'glTF')
        binary_size=len(chunks[1][1]) if len(chunks)>1 else 0
    else:
        require(path.suffix.lower()=='.gltf','expected .glb or .gltf')
        root=mapping(json.loads(data),'glTF')
    require(mapping(root.get('asset'),'asset').get('version')=='2.0','expected glTF 2')
    report=[]
    buffers=objects(root.get('buffers',[]),'buffers')
    views=objects(root.get('bufferViews',[]),'bufferViews')
    for kind,entries in [('buffer',buffers),('image',objects(root.get('images',[]),'images'))]:
        for i,entry in enumerate(entries):
            if 'uri' in entry:
                uri=entry['uri']; require(isinstance(uri,str),'URI must be a string')
                if uri.startswith('data:'):
                    header,sep,body=uri.partition(',')
                    require(sep and header.endswith(';base64'),'only base64 data URIs supported')
                    content=base64.b64decode(body,validate=True); label='embedded'
                else:
                    parsed=urlsplit(uri)
                    name=unquote(parsed.path)
                    require(not parsed.scheme and not parsed.netloc and not parsed.query and not parsed.fragment and '\\' not in name and ':' not in name,'remote/invalid asset URI')
                    target=(path.parent/name).resolve()
                    require(name and not Path(name).is_absolute() and target.is_relative_to(path.parent),'dependency escapes input folder')
                    require(target.is_file(),f'missing {kind}: {name}')
                    content=target.read_bytes(); label=name
                if kind=='buffer': require(type(entry.get('byteLength')) is int and entry['byteLength']==len(content),'buffer byteLength mismatch')
                report.append({'kind':kind,'index':i,'source':label,'bytes':len(content),'sha256':hashlib.sha256(content).hexdigest()})
            elif kind=='buffer':
                require(i==0 and type(entry.get('byteLength')) is int and 0<=entry['byteLength']<=binary_size<=entry['byteLength']+3,'missing embedded buffer')
            else:
                v=entry.get('bufferView'); require(type(v) is int and 0<=v<len(views),'missing image bufferView')
                view=views[v]; b=view.get('buffer',0); start=view.get('byteOffset',0); length=view.get('byteLength')
                require(type(b) is int and 0<=b<len(buffers) and type(start) is int and type(length) is int and start>=0 and length>0 and start+length<=buffers[b]['byteLength'],'image bufferView outside buffer')
                report.append({'kind':kind,'index':i,'source':'bufferView','bytes':length})
    return report

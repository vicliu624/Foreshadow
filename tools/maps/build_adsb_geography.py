"""Reproduce offline ADS-B geography from checksum-pinned Natural Earth GeoJSON.

Python standard library only. Downloading is explicit; application builds use the
committed, bounded resource and never contact a map service.
"""
import argparse,hashlib,json,math,re,urllib.request
from pathlib import Path

REVISION='ca96624a56bd078437bca8184e78163e5039ad19'
SOURCES={
 'countries':('ne_10m_admin_0_countries','239eec57ac17f100a11e2536cffc56752c318b50ae765b0918ff7aab4ce8f255',13287234),
 'regions':('ne_10m_admin_1_states_provinces','22d0e3ad85eb3e27f17cabf8ba2d50e554fbc27a87796ff891d958185da62fb5',40726851),
 'places':('ne_10m_populated_places','9b8e3de09048ef00dfc70357dbb9fa324493f214b5e0ae4daf1aa79a8d10116b',19359003)}
LANGUAGE_ALIASES={'zh':'zh-CN','zht':'zh-TW'}

def simplify(points,tolerance=.01):
    """Ramer-Douglas-Peucker, preserving closed rings and their hole grouping."""
    def rdp(items):
        if len(items)<=2:return items
        a,b=items[0],items[-1];dx,dy=b[0]-a[0],b[1]-a[1];length=dx*dx+dy*dy
        maximum=-1;index=0
        for i,p in enumerate(items[1:-1],1):
            t=max(0,min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/length)) if length else 0
            distance=(p[0]-a[0]-t*dx)**2+(p[1]-a[1]-t*dy)**2
            if distance>maximum:maximum,index=distance,i
        return rdp(items[:index+1])[:-1]+rdp(items[index:]) if maximum>tolerance*tolerance else [a,b]
    rounded=[]
    for point in points:
        value=[round(point[0],5),round(point[1],5)]
        if not rounded or rounded[-1]!=value:rounded.append(value)
    if rounded[0]!=rounded[-1]:rounded.append(rounded[0])
    result=rdp(rounded)
    return result if len(result)>=4 else rounded

def names(properties,locales):
    result={}
    for key,value in properties.items():
        key=key.lower()
        if key.startswith('name_') and isinstance(value,str) and value:
            locale=key[5:]
            if re.fullmatch('[a-z]{2}',locale) or locale in LANGUAGE_ALIASES:
                language=LANGUAGE_ALIASES.get(locale,locale)
                if language in locales:result[language]=value
    result.setdefault('en',properties.get('NAME_EN') or properties.get('name_en') or properties.get('NAME') or properties.get('name') or properties.get('ADMIN') or properties.get('woe_name') or properties.get('gn_name'))
    if not result['en']:raise ValueError('Unnamed geography feature')
    return result

def convert_region(feature,kind,locales):
    properties=feature['properties'];geometry=feature['geometry']
    polygons=[geometry['coordinates']] if geometry['type']=='Polygon' else geometry['coordinates']
    converted=[[simplify(ring) for ring in polygon] for polygon in polygons]
    points=[p for polygon in converted for ring in polygon for p in ring]
    if kind=='countries':
        identity=properties['ADM0_A3'];country=identity;x,y=properties.get('LABEL_X'),properties.get('LABEL_Y')
    else:
        identity=properties['adm1_code'];country=properties['adm0_a3'];x,y=properties.get('longitude'),properties.get('latitude')
    if not isinstance(x,(float,int)) or not isinstance(y,(float,int)) or not(-180<=x<=180 and -90<=y<=90):
        x=sum(p[0] for p in points)/len(points);y=sum(p[1] for p in points)/len(points)
    return {'id':identity,'country':country,'names':names(properties,locales),'label':[round(x,5),round(y,5)],'polygons':converted}

def build(cache,output,download=False):
    result={'schema':1};sources=[];locales={p.stem for p in (output/'i18n').glob('*.json')}|{'en'}
    for kind,(name,sha,size) in SOURCES.items():
        path=cache/(name+'.geojson');url=f'https://raw.githubusercontent.com/nvkelso/natural-earth-vector/{REVISION}/geojson/{name}.geojson'
        if not path.exists() and download:
            cache.mkdir(parents=True,exist_ok=True)
            with path.open('wb') as file:
                for start in range(0,size,4*1024*1024):
                    end=min(size,start+4*1024*1024)-1
                    request=urllib.request.Request(url+f'?range={start}',headers={'Range':f'bytes={start}-{end}'})
                    with urllib.request.urlopen(request,timeout=60) as response:
                        if response.status!=206 or response.headers.get('Content-Range')!=f'bytes {start}-{end}/{size}':raise ValueError('Unexpected source range')
                        chunk=response.read()
                    if len(chunk)!=end-start+1:raise ValueError('Incomplete source download')
                    file.write(chunk)
        if path.stat().st_size!=size or hashlib.file_digest(path.open('rb'),'sha256').hexdigest()!=sha:raise ValueError('Source checksum mismatch: '+name)
        data=json.loads(path.read_text(encoding='utf-8'))
        if kind!='places':
            named=[f for f in data['features'] if f['properties'].get('NAME') or f['properties'].get('name') or f['properties'].get('woe_name') or f['properties'].get('gn_name')]
            result[kind]=[convert_region(f,kind,locales) for f in named]
        else:
            result[kind]=[{'names':names(f['properties'],locales),'country':f['properties']['ADM0_A3'],'position':[round(c,5) for c in f['geometry']['coordinates']],
                'rank':max(0,min(20,int(f['properties']['SCALERANK'])))} for f in data['features']]
            result[kind].sort(key=lambda x:(x['rank'],x['names']['en']))
        sources.append({'layer':kind,'url':url,'source_sha256':sha,'source_bytes':size,'features':len(result[kind]),'omitted_unnamed_features':len(data['features'])-len(result[kind])})
    points=sum(len(ring) for kind in ('countries','regions') for r in result[kind] for polygon in r['polygons'] for ring in polygon)
    raw=(json.dumps(result,ensure_ascii=False,separators=(',',':'))+'\n').encode('utf-8')
    if len(raw)>16*1024*1024 or points>650000:raise ValueError(f'Delivered geography exceeds runtime budgets: {len(raw)} bytes, {points} points')
    output.mkdir(parents=True,exist_ok=True);(output/'geography.json').write_bytes(raw)
    provenance=json.loads((output/'map-source.json').read_text(encoding='utf-8'))
    provenance['geography']={'schema':1,'revision':REVISION,'layers':sources,'terms':'https://www.naturalearthdata.com/about/terms-of-use/',
      'license':'public-domain','language_aliases':LANGUAGE_ALIASES,'exported_name_locales':sorted(locales),'processing':'Preserve country/admin-1 polygon and hole grouping; RDP tolerance 0.01 degrees; coordinates rounded to five decimals; upstream names for installed provider locales and populated places retained. City proximity is distinct from administrative containment.',
      'processed_sha256':hashlib.sha256(raw).hexdigest(),'processed_bytes':len(raw),'polygon_points':points}
    (output/'map-source.json').write_text(json.dumps(provenance,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps({'bytes':len(raw),'points':points,**{k:len(result[k]) for k in ('countries','regions','places')}}))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--cache',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[2]/'plugins/adsb');parser.add_argument('--download',action='store_true')
    args=parser.parse_args();build(args.cache,args.output,args.download)

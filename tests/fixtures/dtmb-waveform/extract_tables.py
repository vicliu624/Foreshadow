from pathlib import Path
import argparse,hashlib,json,re,pymupdf

p=argparse.ArgumentParser(description='Reproduce the numerical fixture tables from the pinned GB 20600-2006 PDF')
p.add_argument('pdf',type=Path);p.add_argument('--output',type=Path,default=Path(__file__).with_name('gb20600-tables.json'));args=p.parse_args()
source=args.pdf
assert hashlib.sha256(source.read_bytes()).hexdigest()=='3dd373d098e0204ef17b77384f8685a4026f9ddc09522d7832a981267a4dca8f','Unexpected standard PDF content'
document=pymupdf.open(source)
text='\n'.join(page.get_text() for page in document)
parts=re.split(r'LDPC\(7493,\s*(3048|4572|6096)\)\s*的?Gi,j=G\[i\]\[j\]',text)
assert len(parts)==7,len(parts)
generators={}
for index in range(1,7,2):
    size=int(parts[index]);k=size//127;c=59-k
    entries=re.findall(r'G\[\s*(\d+)\]\[\s*(\d+)\]\s*:\s*([0-9A-F]{32})',parts[index+1])
    assert len(entries)==k*c,(size,len(entries),k*c)
    rows={}
    for i,j,value in entries:
        key=(int(i),int(j));assert key not in rows;assert int(value,16)<2**127
        rows[key]=value
    generators[str(size)]=[[rows[i,j] for j in range(c)] for i in range(k)]
pn={}
for length,start,stop,count,hexsize in ((420,87,98,225,105),(945,98,117,200,237)):
    # Strip page headers before joining rows split at page boundaries.
    body='\n'.join(re.sub(r'^GB 20600—2006\s+\d+\s*','',document[i].get_text()) for i in range(start,stop))
    entries=list(re.finditer(r'(?:^|\n)\s*(\d+)\s*:\s*',body))
    values=[]
    for i,item in enumerate(entries):
        assert int(item[1])==i
        raw=body[item.end():entries[i+1].start() if i+1<len(entries) else len(body)]
        value=''.join(raw.split());assert re.fullmatch('[0-9A-F]{'+str(hexsize)+'}',value),(length,i,len(value))
        values.append(value)
    assert len(values)==count;pn[str(length)]=values
frequency=[]
for i in range(117,126):
    for item in re.finditer(r'(\d{4}):(\d{4})\s+((?:\d{4}\s+){9}\d{4})',document[i].get_text()):
        assert int(item[1])==len(frequency) and int(item[2])==len(frequency)+9
        frequency.extend(map(int,item[3].split()))
assert len(frequency)==3780 and sorted(frequency)==list(range(3780))
si=re.findall(r'(?:^|\n)\s*\d+\s*\n([01]{32})',document[126].get_text()+'\n'+document[127].get_text())
assert len(si)==64,len(si)
nr={}
for i in (85,86):
    for item in re.finditer(r'(?:^|\n)\s*([0-9A-F]{2})\s+([0-9A-F]{4})(?=\s)',document[i].get_text()):
        nr[int(item[1],16)]=item[2]
assert len(nr)==256,len(nr)
check_text='\n'.join(document[i].get_text() for i in range(79,85))
check_parts=re.split(r'LDPC\(7493,\s*(3048|4572|6096)\)\s*的校验矩阵定义[：:]',check_text)
assert len(check_parts)==7,len(check_parts)
checks={}
for i in range(1,7,2):
    size=int(check_parts[i]);values=re.findall(r'A\[\s*(\d+)\]\[\s*(\d+)\]\s*=\s*(\d+)',check_parts[i+1])
    checks[str(size)]=[[int(a),int(b),int(s)] for a,b,s in values]
    assert len({(int(a),int(b)) for a,b,s in values})==len(values)
result={'standard':'GB 20600-2006','source_url':'https://upload.wikimedia.org/wikipedia/commons/2/2c/GB_20600%E2%80%942006.pdf',
        'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'ldpc_generator_first_rows':generators,'ldpc_check_circulants':checks,
        'pn_headers_hex':pn,'frequency_output_to_input':frequency,'system_info_bits':si,'nr_hex':[nr[i] for i in range(256)]}
target=args.output;target.parent.mkdir(parents=True,exist_ok=True)
target.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
print(json.dumps({'path':str(target),'bytes':target.stat().st_size,'source_sha256':result['source_sha256'],'tables_sha256':hashlib.sha256(target.read_bytes()).hexdigest()}))

import struct
def parse(path):
    r=open(path,'rb').read()
    do,mo,dlen,mlen=struct.unpack('>IIII',r[:16])
    tl=struct.unpack('>H',r[mo+24:mo+26])[0]; nl=struct.unpack('>H',r[mo+26:mo+28])[0]
    n=struct.unpack('>H',r[mo+28:mo+30])[0]+1
    tlist=mo+tl; nlist=mo+nl
    res=[]
    for i in range(n):
        p=tlist+2+i*8
        t=r[p:p+4].decode('mac_roman'); c=struct.unpack('>H',r[p+4:p+6])[0]+1; ro=struct.unpack('>H',r[p+6:p+8])[0]
        for j in range(c):
            q=tlist+ro+j*12
            rid,noff=struct.unpack('>hH',r[q:q+4]); attr=r[q+4]; off=int.from_bytes(r[q+5:q+8],'big')
            name=''
            if noff!=0xFFFF:
                L=r[nlist+noff]; name=r[nlist+noff+1:nlist+noff+1+L].decode('mac_roman')
            ln=struct.unpack('>I',r[do+off:do+off+4])[0]
            res.append((t,rid,name,attr,r[do+off+4:do+off+4+ln]))
    return res

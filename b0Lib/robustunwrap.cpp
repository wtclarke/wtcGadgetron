/*
 3D Unwrapping algorithm
 Rhodri Cusack 2000-2006
 Algorithm described in 
 Cusack, R. & Papadakis, N. (2002) 
    "New robust 3-D phase unwrapping algorithms: application to magnetic field mapping and undistorting echoplanar images."
    Neuroimage. 2002 Jul;16(3 Pt 1):754-64.
 Distributed under GNU public license http://www.gnu.org/copyleft/gpl.html
 Comments to rhodri.cusack@mrc-cbu.cam.ac.uk
 Version 3.00 Oct 2006 adapted for matlab
 */

#include "robustunwrap.h"
#include <cmath>
#include "log.h"

#define PI 3.14159265358979
#define FILEFORMAT_CAMRES 1
#define FILEFORMAT_ANALYZE 2
#define MAXFILENAMESIZE 1024

int errcode;
long dim[3];
long sze;
long polesze;
float *phase,*mag;
float *unwrapped;
bool *flag;
long m_bsx,m_bsy,m_bsz;

struct FIELD
{
    double d;	/* value of field */
    long p[3];  /* at this offset */
    int x,y,z;
};

struct QUEUEENTRY
{
    int x,y,z;
    long p;
    double v;
};

struct FIELD_2
{
    double d;	/*value of field*/
    long p;		/*at this offset*/
    int x,y,z;
};


void raiseerror(const char *msg)
{
    //mexErrMsgTxt(msg);
    GERROR(msg);
    errcode=1;
}

const long NUMQUEUES=10000;
const long DEFAULTBLOCK=100;
const long BLOCKINCREMENT=500;

long m_bot[NUMQUEUES], m_top[NUMQUEUES];
long m_size[NUMQUEUES], m_chunk[NUMQUEUES], m_sizeb[NUMQUEUES];
char *m_q[NUMQUEUES];

void InitQueue(int queuenum,int chunk)
{
    m_chunk[queuenum]=chunk;
    m_bot[queuenum]=0;
    m_top[queuenum]=0;
    m_size[queuenum]=DEFAULTBLOCK;
    m_sizeb[queuenum]=m_size[queuenum]*m_chunk[queuenum];
    
    m_q[queuenum]=0;
};

void TerminateQueue(int queuenum)
{
    if (m_q[queuenum]) delete m_q[queuenum];
    m_q[queuenum]=0;
};




int Push(int queuenum,void *x)
{
    if (!m_q[queuenum]) m_q[queuenum]=new char[m_sizeb[queuenum]];
    if (!m_q[queuenum])
    {
        printf("Out of memory - could not generate new point queue.");
        return(-1);
    };
    
    int stacksize;
    stacksize=m_top[queuenum]-m_bot[queuenum];
    if (stacksize<0) stacksize+=m_size[queuenum];
    memcpy(m_q[queuenum]+m_top[queuenum]*m_chunk[queuenum],x,m_chunk[queuenum]);
    m_top[queuenum]++;
    
    if (m_top[queuenum]==m_size[queuenum]) m_top[queuenum]=0;
    
    if (m_top[queuenum]==m_bot[queuenum])
    {
        char *newq;
        long newsize, newsizeb, abovebot_b;
        // previously out of memory - now auto-expand
        newsize=m_size[queuenum]+BLOCKINCREMENT;
        newsizeb=newsize*m_chunk[queuenum];
        newq=new char[newsizeb];
        if (!newq)
        {
            printf("Out of memory - point queue full.");
            return(-1);
        };
        // While we're shifting circular buffer, it is actually easier to re-origin it
        // to zero.
        // first, copy top bit from m_bot upwards
        abovebot_b=(m_size[queuenum]-m_bot[queuenum])*m_chunk[queuenum];
        if (abovebot_b>0) memcpy(newq,(char *)m_q[queuenum]+m_bot[queuenum]*m_chunk[queuenum],abovebot_b);
        // then, do m_top downwards
        if (m_top[queuenum]!=0) memcpy((char *) newq+abovebot_b,m_q[queuenum],m_top[queuenum]*m_chunk[queuenum]);
        m_bot[queuenum]=0;
        m_top[queuenum]=m_size[queuenum];
        m_size[queuenum]=newsize;
        m_sizeb[queuenum]=newsizeb;
        delete m_q[queuenum]; // recover old memory
        m_q[queuenum]=newq;
    };
    
    return(0);
    
};

int Pop(int queuenum,void *x)
{
    if (m_bot[queuenum]==m_top[queuenum]) return(1);
    
    memcpy(x,m_q[queuenum]+m_bot[queuenum]*m_chunk[queuenum],m_chunk[queuenum]);
    m_bot[queuenum]++;
    if (m_bot[queuenum]==m_size[queuenum]) m_bot[queuenum]=0;
    
    return(0);
};


void Check(int primaryqueuenum, QUEUEENTRY* qe,long offp, int offx, int offy, int offz)
{
    /* first check bounds */
    QUEUEENTRY nqe;
    nqe.x=qe->x+offx;
    if ((nqe.x<0)||(nqe.x>=dim[0])) return;
    nqe.y=qe->y+offy;
    if ((nqe.y<0)||(nqe.y>=dim[1])) return;
    nqe.z=qe->z+offz;
    if ((nqe.z<0)||(nqe.z>=dim[2])) return;
    
    
    nqe.p=qe->p+offp;
    
    if (flag[nqe.p]) return; /* Already been here */
    
    
    /* Actually do unwrap */
    int wholepis;
    wholepis=int((phase[nqe.p]-qe->v)/PI);
    
    if (wholepis>=1)
        nqe.v=(double) phase[nqe.p]-2*PI*int((wholepis+1)/2);
    else if (wholepis<=-1)
        nqe.v=(double) phase[nqe.p]+2*PI*int((1-wholepis)/2);
    else nqe.v=phase[nqe.p];
    
    unwrapped[nqe.p]=nqe.v;
    flag[nqe.p]=true;
    
    if (Push(primaryqueuenum,&nqe))
        raiseerror("Out of memory!");
    
    
};


void unwrap(int seedx, int seedy, int seedz,long UNWRAPBINS)
{
    long i;
    long seedp;
    
    errcode=0;
    
    /* Minimum number of unwrapping bins is 2 */
    if (UNWRAPBINS<2) UNWRAPBINS=2;
    /* Find min and max */
    double min,max;
    min=(double) 1e38;
    max=0;
    
    for (i=0; i<sze; i++)
    {
        min=mag[i]<min?mag[i]:min;
        max=mag[i]>max?mag[i]:max;
    };
    
    
    double diff;
    diff=(double) 1.00001*(max-min);
    
    seedp=seedx+dim[0]*(seedy+dim[1]*seedz);
    
    flag=new bool[sze];
    
    if (!flag) raiseerror("Out of memory. ");
    for (i=0; i<sze; i++)
        flag[i]=false;
    
    
    for (i=0; i<UNWRAPBINS; i++)
        InitQueue(i,sizeof(QUEUEENTRY));
    QUEUEENTRY qe;
    
    qe.p=seedp;
    qe.x=seedx;
    qe.y=seedy;
    qe.z=seedz;
    unwrapped[85]=phase[85];
    unwrapped[seedp]=qe.v=phase[seedp];
    flag[seedp]=true;
    
/* push seed */
    Push(0,&qe);
    
    
    int opc,pc;
    opc=-1;
    
/* First, work out pole field threshold that we're going to use */
    double *polefieldthresholds;
    int ind=0;
    int voxdone,voxdef;
    
    polefieldthresholds=new double [UNWRAPBINS];
    voxdef=voxdone=0;
    
    for (i=0; i<(UNWRAPBINS); i++)
        polefieldthresholds[ind++]=min+diff*i/(UNWRAPBINS-1); 
    opc=-10;
    
    for (i=0; i<UNWRAPBINS ; i++)
    {
        voxdef=0;
        while (!errcode && !Pop(i,&qe))
        {
            if (mag[qe.p]>polefieldthresholds[i])  /* too close to a scary pole, so just defer by pushing to other stack */
            {
                ind=i;
                while (mag[qe.p]>polefieldthresholds[++ind]);
                Push(ind,&qe);										/* just defer by pushing to relevant stack */
                voxdef++;
            }
            else
            {
                Check(i,&qe,+m_bsz,0,0,1);
                Check(i,&qe,-m_bsz,0,0,-1);
                Check(i,&qe,+m_bsy,0,1,0);
                Check(i,&qe,-m_bsy,0,-1,0);
                Check(i,&qe,+m_bsx,1,0,0);
                Check(i,&qe,-m_bsx,-1,0,0);
                voxdone++;
            };
        };
        TerminateQueue(i);	/* done with this Queue so free up memory */
        
        if (errcode) break;
    };
    
    delete flag;
}


void robustUnwrapMain(std::vector<size_t> dims, float* phaseIn, float* magIn, float* output, const int numunwrapbins)
{
    long i,j,k;
    int seedx,seedy,seedz;
    int ndims;
    float *maginput;

    ndims = (int)dims.size();

    seedx=int(std::round(double(dims[0])/2.0)-1.0);
    seedy=int(std::round(double(dims[1])/2.0)-1.0);
    seedz=int(std::round(double(dims[2])/2.0)-1.0);
    
    dim[0] = (long)dims[0];
    dim[1] = (long)dims[1];
    dim[2] = (long)dims[2];

    sze = dims[0]*dims[1]*dims[2];

    if (seedx<0 || seedx>=dims[0] || seedy<0 || seedy>=dims[1] || seedz<0 || seedz>=dims[2])
        GERROR("The seed specified was outside the matrix bounds.");
    
    phase=phaseIn;
    /* Negate input as low polefield values unwrapped first */
    maginput=magIn;
    mag=new float[sze];
    for(i=0; i<sze; i++)
        mag[i]=-maginput[i];

    unwrapped=output;

    m_bsx=1;
    m_bsy=dims[0];
    m_bsz=dims[0]*dims[1];
  /* Assign pointers to each input and output. */
    unwrap(seedx,seedy,seedz,numunwrapbins);
    
    
}


#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "Vec2.h"
#include "Vec3.h"
//#include "Mat3.h"

#include "VecN.h"
#include "CG.h"

//CG cg;

double* work   = 0;
double* work2D = 0;

Vec2i   ns_2d = (Vec2i){0,0};

int     nkernel      = 0;
double* kernel_coefs = 0;

int nConvPerCG = 1;

// ============== Functions

inline double Bspline(double x){
    double absx = fabs(x);
    if(absx>2) return 0;
    double xx = x*x;
    if(absx>1) return   -absx*(xx+12) + 6*xx + 8;
    return             3*absx* xx     - 6*xx + 4;
}

// Reflective Boundary Conditions
inline int rbc_left (int ijm      ){               return abs(ijm);                   };
inline int rbc_right(int ijm,int n){               return (ijm>=n) ? 2*n-ijm-2 : ijm; }; 
inline int rbc      (int ijm,int n){ ijm=abs(ijm); return (ijm>=n) ? 2*n-ijm-2 : ijm; }; 
inline double dot_rbcL(const double* a, const double* b, const int im, const  int m             ){ double sum=0;               for(int j=0; j<m; j++){               sum += a[j]*b[abs(im+j)                ]; }; return sum; };
inline double dot_rbcR(const double* a, const double* b, const int im, const  int m,const  int n){ double sum=0; int n2=n*2-2; for(int j=0; j<m; j++){ int ijm=im+j; sum += a[j]*b[(ijm>=n) ? (n2-ijm) : ijm]; }; return sum; };

inline void conv1D( const int m, const int n, const double* coefs, const double* x, double* y ){
    int mtot=m+m+1;
    int n_=n-m;
    for(int i=0;  i<m;  i++){ y[i] = dot_rbcL ( coefs, x, i-m, mtot    ); }
    for(int i=m;  i<n_; i++){ y[i] = VecN::dot( mtot , x +i-m, coefs   ); }
    for(int i=n_; i<n;  i++){ y[i] = dot_rbcR ( coefs, x, i-m, mtot, n ); }
    //printf( "\n" );
}

inline void conv1D_down( const int m, const int n, const int di, const double* coefs, const double* x, double* y ){
    const int mtot=2*m+1;
    const int my=m/di;
    const int n_=n-my;
    const int nx=n*di;
    int j0 = -m;
    for(int i=0;  i<m;  i++){ y[i] = dot_rbcL ( coefs, x, j0, mtot     ); j0+=di; }
    for(int i=m;  i<n_; i++){ y[i] = VecN::dot( mtot,  x +j0, coefs    ); j0+=di; }
    for(int i=n_; i<n;  i++){ y[i] = dot_rbcR ( coefs, x, j0, mtot, nx ); j0+=di; }
    //printf( "\n" );
}

inline void conv1D_up( const int m, const int di, const int n, const double* coefs, const double* x, double* y ){
    const int mtot=2*m-1;
    const int my=m*di;
    const int ny=n*di;
    const int n_=ny-my;
    int ic = 0;
    int icmax= di*mtot;
    printf( "  m %i di %i mtot %i my %i ny %i n_ %i icmax %i \n", m, di, mtot, my, ny, n_, icmax );
    //for(int i=0;  i<my;  i++){ y[i] = dot_rbcL ( coefs+ic, x, i-m, mtot    ); ic+=mtot; ic=(ic>=icmax)?0:ic; }
    for(int i=m;  i<n_;  i++){ y[i] = VecN::dot( mtot,  x +i-m, coefs+ic   ); ic+=mtot; ic=(ic>=icmax)?0:ic; }
    //for(int i=n_; i<ny;  i++){ y[i] = dot_rbcR ( coefs+ic, x, i-m, mtot, n ); ic+=mtot; ic=(ic>=icmax)?0:ic; }
    //printf( "\n" );
}

//   Mult  y = A * x
void conv2D_tensorProd( const int ord, const Vec2i& ns, const double* coefs, const double* x, double* y ){
    const int ordsym = ord*2 + 1; 
    if(work==0){ work = new double[ns.x]; }
    for(int iy=0; iy<ns.y; iy++){
        double* yi = y + iy*ns.x;
        VecN::set( ns.x, 0.0, work );
        for(int ky=0; ky<ordsym; ky++ ){
            int jx = rbc( iy-ord+ky, ns.y )*ns.x;
            VecN::add_mul( ns.x, coefs[ky], x+jx, work );
        }
        conv1D( ord, ns.x, coefs, work, yi );
    }
}

void conv2D_tensorProd_down( const int ord, const int di, const Vec2i& ns, const double* coefs, const double* x, double* y ){
    const int ordsym = ord*2 + 1; 
    const int nx_in = ns.x*di;
    const int ny_in = ns.y*di;
    printf( " conv2D_tensorProd_down %i %i %i %i \n",  ord, di, ns.x, nx_in );
    if(work==0){ work = new double[nx_in]; }
    int j = -ord;
    for(int iy=0; iy<ns.y; iy++){
        printf( "iy  %i | %i %i \n", iy, ns.x, nx_in  );
        VecN::set( nx_in, 0.0, work );
        for(int ky=0; ky<ordsym; ky++ ){
            int jx = rbc(j+ky, ny_in )*nx_in;
            printf( " iy,ky %i,%i (%i,%i) -> [%i]: %g \n", iy, ky, j, j+ky, rbc(j+ky, ny_in ), coefs[ky] );
            VecN::add_mul( nx_in, coefs[ky], x+jx, work );
        }
        j+=di;
        //for(int ix=0;ix<nx_in;ix++){ work[ix]=(x+(iy*di)*nx_in )[ix]; }
        //printf( "   -1 iy  %i | %i %i \n", iy, ns.x, nx_in  );
        double* yi = y + iy*ns.x;
        //for(int ix=0;ix<ns.x;ix++){ yi[ix]=work[ix*di]; }
        conv1D_down( ord, ns.x, di, coefs, work, yi );
        //printf( "   -2 iy  %i | %i %i \n", iy, ns.x, nx_in  );
    }
    printf( "DONE \n" );
}

void dotFunc_conv2D_tensorProd( int n,const double * x, double * Ax ){
    //printf( "dotFunc_conv2D_tensorProd n %i \n", n );
    //for(int i=0; i<n; i++){ Ax[i]=0; }
    if(nConvPerCG==1){
        conv2D_tensorProd( nkernel, ns_2d, kernel_coefs, x, Ax );
    }else{
        double* out1=work2D;
        double* out2=Ax;
        conv2D_tensorProd( nkernel, ns_2d, kernel_coefs, x, out1 );
        for(int itr=1;itr<nConvPerCG;itr++){
            double* tmp=out1; out1=out2; out2=tmp;
            conv2D_tensorProd( nkernel, ns_2d, kernel_coefs, out2, out1 );
        }
        if( nConvPerCG & 1 ) for(int i=0; i<n; i++){ Ax[i]=out2[i]; }
    }
};

// DEBUG
CG cg_glob;

extern "C"{

void convolve1D(int m,int di,int n,double* coefs, double* x, double* y ){ 
    //conv1D( m, n-m*2, coefs, x, y+m ); 
    printf( " m %i di %i n %i \n", m, di, n );
    if(di== 1){ conv1D     ( m,      n, coefs, x, y ); }
    if(di<0  ){ conv1D_up  ( m, -di, n, coefs, x, y ); }
    else      { conv1D_down( m,  di, n, coefs, x, y ); }
}

void convolve2D_tensorProduct( int ord, int di, int nx, int ny, double* coefs, double* x, double* y ){
    if(di== 1){ conv2D_tensorProd     ( ord,     (Vec2i){nx,ny}, coefs, x, y ); }
    //if(di==-1){ conv2D_tensorProd_up  ( ord, di, (Vec2i){nx,ny}, coefs, x, y ); }
    else      { conv2D_tensorProd_down( ord, di, (Vec2i){nx,ny}, coefs, x, y ); }
    printf( "DONE 1\n" );
    delete [] work; work=0;
    printf( "DONE 2\n" ); 
}

void solveCG( int n, double* A, double* b, double* x, int maxIters, double maxErr ){
    CG cg( n, x, b, A );
    cg.solve_CG( maxIters, maxErr, true );
}

void fit_tensorProd( int ord, int nx, int ny, double* kernel_coefs_, double* BYref, double* Ycoefs, int maxIters, double maxErr, int nConvPerCG_ ){
    nConvPerCG = nConvPerCG_;
    if(nConvPerCG>0) work2D = new double[nx*ny];
    nkernel = ord;
    ns_2d   = (Vec2i){nx,ny};
    kernel_coefs = kernel_coefs_;
    printf( " ns_2d %i,%i \n", ns_2d.x, ns_2d.y );
    CG cg( nx*ny, Ycoefs, BYref );
    cg.dotFunc = dotFunc_conv2D_tensorProd;
    //cg.step_CG();
    //printf( " to CG ... \n" );
    cg.solve_CG( maxIters, maxErr, true );
    //cg.solve_GD( maxIters*5, maxErr, 100.0,  true );
    delete [] work; work=0;
    if(nConvPerCG>0){ delete [] work2D; work2D=0; }
}

void setup_fit_tensorProd( int ord, int nx, int ny, double* kernel_coefs_, double* BYref, double* Ycoefs, double* Wprecond, int nConvPerCG_ ){
    if(nConvPerCG_>0) work2D = new double[nx*ny];
    nConvPerCG = nConvPerCG_;
    nkernel = ord;
    ns_2d   = (Vec2i){nx,ny};
    kernel_coefs = kernel_coefs_;
    printf( " ns_2d %i,%i \n", ns_2d.x, ns_2d.y );
    cg_glob.setLinearProblem( nx*ny, Ycoefs, BYref );
    printf( "Wprecond %li \n", (long)Wprecond );
    if(Wprecond) cg_glob.w = Wprecond;
    cg_glob.dotFunc = dotFunc_conv2D_tensorProd;
    //delete [] work; work=0;
}

void step_fit_tensorProd( ){
    double err2 = cg_glob.step_CG();
    printf( "CG[%i] err %g \n", cg_glob.istep, sqrt(err2) );
}


}

 
 
 























 











































































































 




 
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <math.h>    
#include <stddef.h>  
#include <stdlib.h>  
#include <string.h>  
#include <stdio.h>   
#include "cmaes_interface.h"  

 
 
 

 

 

long   cmaes_random_init(cmaes_random_t *, long unsigned seed  );
void   cmaes_random_exit(cmaes_random_t *);
double cmaes_random_Gauss(cmaes_random_t *);  
double cmaes_random_Uniform(cmaes_random_t *);
long   cmaes_random_Start(cmaes_random_t *, long unsigned seed  );

void   cmaes_timings_init(cmaes_timings_t *timing);
void   cmaes_timings_start(cmaes_timings_t *timing);  
double cmaes_timings_update(cmaes_timings_t *timing);
void   cmaes_timings_tic(cmaes_timings_t *timing);
double cmaes_timings_toc(cmaes_timings_t *timing);

void cmaes_readpara_init (cmaes_readpara_t *, int dim, const double * xstart, 
                    const double * sigma, int seed, int lambda,
                    const char * filename);
void cmaes_readpara_exit(cmaes_readpara_t *);
void cmaes_readpara_ReadFromFile(cmaes_readpara_t *, const char *szFileName);
void cmaes_readpara_SupplementDefaults(cmaes_readpara_t *);
void cmaes_readpara_SetWeights(cmaes_readpara_t *, const char * mode);
void cmaes_readpara_WriteToFile(cmaes_readpara_t *, const char *filenamedest);

const double * cmaes_Optimize( cmaes_t *, double(*pFun)(double const *, int dim), 
                                long iterations);
double const * cmaes_SetMean(cmaes_t *, const double *xmean);
double * cmaes_PerturbSolutionInto(cmaes_t *t, double *xout, 
                                   double const *xin, double eps);
void cmaes_WriteToFile(cmaes_t *, const char *key, const char *name);
void cmaes_WriteToFileAW(cmaes_t *t, const char *key, const char *name, 
                         const char * append);
void cmaes_WriteToFilePtr(cmaes_t *, const char *key, FILE *fp);
void cmaes_ReadFromFilePtr(cmaes_t *, FILE *fp); 
void cmaes_FATAL(char const *s1, char const *s2, 
                 char const *s3, char const *s4);


 

static char * getTimeStr(void); 
static void TestMinStdDevs( cmaes_t *);
 

static void Eigen( int N,  double **C, double *diag, double **Q, 
                   double *rgtmp);
static int  Check_Eigen( int N,  double **C, double *diag, double **Q);
static void QLalgo2 (int n, double *d, double *e, double **V); 
static void Householder2(int n, double **V, double *d, double *e); 
static void Adapt_C2(cmaes_t *t, int hsig);

static void FATAL(char const *sz1, char const *s2, 
                  char const *s3, char const *s4);
static void ERRORMESSAGE(char const *sz1, char const *s2, 
                         char const *s3, char const *s4);
static int isNoneStr(const char * filename);
static void   Sorted_index( const double *rgFunVal, int *index, int n);
static int    SignOfDiff( const void *d1, const void * d2);
static double douSquare(double);
static double rgdouMax( const double *rgd, int len);
static double rgdouMin( const double *rgd, int len);
static double douMax( double d1, double d2);
static double douMin( double d1, double d2);
static int    MaxIdx( const double *rgd, int len);
static int    MinIdx( const double *rgd, int len);
static double myhypot(double a, double b);
static double * new_double( int n);
static void * new_void( int n, size_t size); 
static char * new_string( const char *); 
static void assign_string( char **, const char*);

static const char * c_cmaes_version = "3.20.00.beta";

 
 
 

static char *
getTimeStr(void) {
  time_t tm = time(NULL);
  static char s[33];

   
  strncpy(s, ctime(&tm), 24);  
  s[24] = '\0';  
  return s;
}

char * 
cmaes_SayHello(cmaes_t *t)
{
   
  sprintf(t->sOutString, 
          "(%d,%d)-CMA-ES(mu_eff=%.1f), Ver=\"%s\", dimension=%d, diagonalIterations=%ld, randomSeed=%d (%s)", 
          t->sp.mu, t->sp.lambda, t->sp.mueff, t->version, t->sp.N, (long)t->sp.diagonalCov,
          t->sp.seed, getTimeStr());

  return t->sOutString; 
}

 
 
void
cmaes_init_para(cmaes_t *t,  
                int dimension, 
                double *inxstart,
                double *inrgstddev,  
                long int inseed,
                int lambda, 
                const char *input_parameter_filename) 
{
  t->version = c_cmaes_version;
  cmaes_readpara_init(&t->sp, dimension, inxstart, inrgstddev, inseed, 
                   lambda, input_parameter_filename);
}

double * 
cmaes_init_final(cmaes_t *t  )

 
{
  int i, j, N;
  double dtest, trace;
  
  if (t->version == NULL) {
        ERRORMESSAGE("cmaes_init_final called (probably) without calling cmaes_init_para first",
                     ", which will likely lead to unexpected results",0,0);
        printf("Error: cmaes_init_final called (probably) without calling cmaes_init_para first\n");
  }
  if (strcmp(c_cmaes_version, t->version) != 0) {
        ERRORMESSAGE("cmaes_init_final called twice, which will lead to a memory leak",
                     "; use cmaes_exit() first",0,0);
        printf("Error: cmaes_init_final called twice, which will lead to a memory leak; use cmaes_exit first\n");
  }
   

  if (!t->sp.flgsupplemented) {
    cmaes_readpara_SupplementDefaults(&t->sp);
    if (!isNoneStr(t->sp.filename))  
      cmaes_readpara_WriteToFile(&t->sp, "actparcmaes.par");
  }
     
  t->sp.seed = cmaes_random_init( &t->rand, (long unsigned int) t->sp.seed);

  N = t->sp.N;  
  
   
  for (i = 0, trace = 0.; i < N; ++i)
    trace += t->sp.rgInitialStds[i]*t->sp.rgInitialStds[i];
  t->sigma = sqrt(trace/N);  

  t->chiN = sqrt((double) N) * (1. - 1./(4.*N) + 1./(21.*N*N));
  t->flgEigensysIsUptodate = 1;
  t->flgCheckEigen = 0; 
  t->genOfEigensysUpdate = 0;
  cmaes_timings_init(&t->eigenTimings);
  t->flgIniphase = 0;  
  t->flgresumedone = 0;
  t->flgStop = 0;

  for (dtest = 1.; dtest && dtest < 1.1 * dtest; dtest *= 2.) 
    if (dtest == dtest + 1.)
      break;
  t->dMaxSignifKond = dtest / 1000.;  

  t->gen = 0;
  t->countevals = 0;
  t->state = 0;
  t->dLastMinEWgroesserNull = 1.0;
  t->printtime = t->writetime = t->firstwritetime = t->firstprinttime = 0; 

  t->rgpc = new_double(N);
  t->rgps = new_double(N);
  t->rgdTmp = new_double(N+1);
  t->rgBDz = new_double(N);
  t->rgxmean = new_double(N+2); t->rgxmean[0] = N; ++t->rgxmean;
  t->rgxold = new_double(N+2); t->rgxold[0] = N; ++t->rgxold; 
  t->rgxbestever = new_double(N+3); t->rgxbestever[0] = N; ++t->rgxbestever; 
  t->rgout = new_double(N+2); t->rgout[0] = N; ++t->rgout;
  t->rgD = new_double(N);
  t->C = (double**)new_void(N, sizeof(double*));
  t->B = (double**)new_void(N, sizeof(double*));
  t->publicFitness = new_double(t->sp.lambda); 
  t->rgFuncValue = new_double(t->sp.lambda+1); 
  t->rgFuncValue[0]=t->sp.lambda; ++t->rgFuncValue;
  t->arFuncValueHist = new_double(10+(int)ceil(3.*10.*N/t->sp.lambda)+1);
  t->arFuncValueHist[0] = (double)(10+(int)ceil(3.*10.*N/t->sp.lambda));
  t->arFuncValueHist++; 

  for (i = 0; i < N; ++i) {
      t->C[i] = new_double(i+1);
      t->B[i] = new_double(N);
    }
  t->index = (int *) new_void(t->sp.lambda, sizeof(int));
  for (i = 0; i < t->sp.lambda; ++i) 
    t->index[i] = i;  
  t->rgrgx = (double **)new_void(t->sp.lambda, sizeof(double*));
  for (i = 0; i < t->sp.lambda; ++i) {
    t->rgrgx[i] = new_double(N+2);
    t->rgrgx[i][0] = N; 
    t->rgrgx[i]++;
  }

   

  for (i = 0; i < N; ++i)
    for (j = 0; j < i; ++j)
       t->C[i][j] = t->B[i][j] = t->B[j][i] = 0.;
        
  for (i = 0; i < N; ++i)
    {
      t->B[i][i] = 1.;
      t->C[i][i] = t->rgD[i] = t->sp.rgInitialStds[i] * sqrt(N / trace);
      t->C[i][i] *= t->C[i][i];
      t->rgpc[i] = t->rgps[i] = 0.;
    }

  t->minEW = rgdouMin(t->rgD, N); t->minEW = t->minEW * t->minEW;
  t->maxEW = rgdouMax(t->rgD, N); t->maxEW = t->maxEW * t->maxEW; 

  t->maxdiagC=t->C[0][0]; for(i=1;i<N;++i) if(t->maxdiagC<t->C[i][i]) t->maxdiagC=t->C[i][i];
  t->mindiagC=t->C[0][0]; for(i=1;i<N;++i) if(t->mindiagC>t->C[i][i]) t->mindiagC=t->C[i][i];

   
  for (i = 0; i < N; ++i)
    t->rgxmean[i] = t->rgxold[i] = t->sp.xstart[i]; 
   
  if (t->sp.typicalXcase) 
    for (i = 0; i < N; ++i)
      t->rgxmean[i] += t->sigma * t->rgD[i] * cmaes_random_Gauss(&t->rand);

  if (strcmp(t->sp.resumefile, "_no_")  != 0)
    cmaes_resume_distribution(t, t->sp.resumefile);

  return (t->publicFitness); 

}  


 
 
double * 
cmaes_init(cmaes_t *t,  
                int dimension, 
                double *inxstart,
                double *inrgstddev,  
                long int inseed,
                int lambda, 
                const char *input_parameter_filename) 
{
  cmaes_init_para(t, dimension, inxstart, inrgstddev, inseed, 
                   lambda, input_parameter_filename);
  return cmaes_init_final(t);
}

 
 

#ifdef __GNUC__
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-result"
#endif

void 
cmaes_resume_distribution(cmaes_t *t, char *filename)
{
  int i, j, res, n; 
  double d; 
  FILE *fp = fopen( filename, "r"); 
  if(fp == NULL) {
    ERRORMESSAGE("cmaes_resume_distribution(): could not open '", 
                 filename, "'",0);
    return;
  }
   
  i = 0; res = 0;
  while (1) {
    if ((res = fscanf(fp, " resume %lg", &d)) == EOF)
      break;
    else if (res==0) 
      fscanf(fp, " %*s");
    else if(res > 0)
      i += 1;
  }

   
  n = i; i = 0; res = 0; rewind(fp);
  while (i<n) {
    if ((res = fscanf(fp, " resume %lg", &d)) == EOF)
      FATAL("cmaes_resume_distribution(): Unexpected error, bug",0,0,0); 
    else if (res==0) 
      fscanf(fp, " %*s");
    else if(res > 0)
      ++i;
  }
  if (d != t->sp.N)
    FATAL("cmaes_resume_distribution(): Dimension numbers do not match",0,0,0); 
  
     
  while (1) {
    if ((res = fscanf(fp, " xmean %lg", &d)) == EOF)
      FATAL("cmaes_resume_distribution(): 'xmean' not found",0,0,0); 
    else if (res==0) 
      fscanf(fp, " %*s");
    else if(res > 0)
      break;
  }
  
   
  t->rgxmean[0] = d; res = 1; 
  for(i = 1; i < t->sp.N; ++i)
    res += fscanf(fp, " %lg", &t->rgxmean[i]);
  if (res != t->sp.N)
    FATAL("cmaes_resume_distribution(): xmean: dimensions differ",0,0,0); 

     
  while (1) {
    if ((res = fscanf(fp, " path for sigma %lg", &d)) == EOF)
      FATAL("cmaes_resume_distribution(): 'path for sigma' not found",0,0,0); 
    else if (res==0) 
      fscanf(fp, " %*s");
    else if(res > 0)
      break;
  }
  
   
  t->rgps[0] = d; res = 1;
  for(i = 1; i < t->sp.N; ++i)
    res += fscanf(fp, " %lg", &t->rgps[i]);
  if (res != t->sp.N)
    FATAL("cmaes_resume_distribution(): ps: dimensions differ",0,0,0); 
  
     
  while (1) {
    if ((res = fscanf(fp, " path for C %lg", &d)) == EOF)
      FATAL("cmaes_resume_distribution(): 'path for C' not found",0,0,0); 
    else if (res==0) 
      fscanf(fp, " %*s");
    else if(res > 0)
      break;
  }
   
  t->rgpc[0] = d; res = 1;
  for(i = 1; i < t->sp.N; ++i)
    res += fscanf(fp, " %lg", &t->rgpc[i]);
  if (res != t->sp.N)
    FATAL("cmaes_resume_distribution(): pc: dimensions differ",0,0,0); 

     
  while (1) {
    if ((res = fscanf(fp, " sigma %lg", &d)) == EOF)
      FATAL("cmaes_resume_distribution(): 'sigma' not found",0,0,0); 
    else if (res==0) 
      fscanf(fp, " %*s");
    else if(res > 0)
      break;
  }
  t->sigma = d;

   
  while (1) {
    if ((res = fscanf(fp, " covariance matrix %lg", &d)) == EOF)
      FATAL("cmaes_resume_distribution(): 'covariance matrix' not found",0,0,0); 
    else if (res==0) 
      fscanf(fp, " %*s");
    else if(res > 0)
      break;
  }
   
  t->C[0][0] = d; res = 1;
  for (i = 1; i < t->sp.N; ++i)
    for (j = 0; j <= i; ++j)
      res += fscanf(fp, " %lg", &t->C[i][j]);
  if (res != (t->sp.N*t->sp.N+t->sp.N)/2)
    FATAL("cmaes_resume_distribution(): C: dimensions differ",0,0,0); 
   
  fclose(fp);
  
  t->flgIniphase = 0;
  t->flgEigensysIsUptodate = 0;
  t->flgresumedone = 1;
  cmaes_UpdateEigensystem(t, 1);
  
}  
#ifdef __GNUC__
    #pragma GCC diagnostic pop
#endif
 
 

void 
cmaes_exit(cmaes_t *t)
{
  int i, N = t->sp.N;
  t->version = NULL; 
   
  t->state = -1;  
  free( t->rgpc);
  free( t->rgps);
  free( t->rgdTmp);
  free( t->rgBDz);
  free( --t->rgxmean);
  free( --t->rgxold); 
  free( --t->rgxbestever); 
  free( --t->rgout); 
  free( t->rgD);
  for (i = 0; i < N; ++i) {
    free( t->C[i]);
    free( t->B[i]);
  }
  for (i = 0; i < t->sp.lambda; ++i) 
    free( --t->rgrgx[i]);
  free( t->rgrgx); 
  free( t->C);
  free( t->B);
  free( t->index);
  free( t->publicFitness);
  free( --t->rgFuncValue);
  free( --t->arFuncValueHist);
  cmaes_random_exit (&t->rand);
  cmaes_readpara_exit (&t->sp); 
}  


 
 
double const * 
cmaes_SetMean(cmaes_t *t, const double *xmean)



 
{
  int i, N=t->sp.N;

  if (t->state >= 1 && t->state < 3)
    FATAL("cmaes_SetMean: mean cannot be set inbetween the calls of ",
          "SamplePopulation and UpdateDistribution",0,0);

  if (xmean != NULL && xmean != t->rgxmean)
    for(i = 0; i < N; ++i)
      t->rgxmean[i] = xmean[i];
  else 
    xmean = t->rgxmean; 

  return xmean; 
}
 
 
 
double * const * 
cmaes_SamplePopulation(cmaes_t *t)
{
  int iNk, i, j, N=t->sp.N;
  int flgdiag = ((t->sp.diagonalCov == 1) || (t->sp.diagonalCov >= t->gen)); 
  double sum;
  double const *xmean = t->rgxmean; 

   

   
  if (!t->flgEigensysIsUptodate) {
    if (!flgdiag)
      cmaes_UpdateEigensystem(t, 0);
    else {
        for (i = 0; i < N; ++i)
          t->rgD[i] = sqrt(t->C[i][i]);
        t->minEW = douSquare(rgdouMin(t->rgD, N)); 
        t->maxEW = douSquare(rgdouMax(t->rgD, N));
        t->flgEigensysIsUptodate = 1;
        cmaes_timings_start(&t->eigenTimings);
      }
  }

   
  TestMinStdDevs(t); 

  for (iNk = 0; iNk < t->sp.lambda; ++iNk)
    {  
      for (i = 0; i < N; ++i)
        if (flgdiag)
          t->rgrgx[iNk][i] = xmean[i] + t->sigma * t->rgD[i] * cmaes_random_Gauss(&t->rand);
        else
          t->rgdTmp[i] = t->rgD[i] * cmaes_random_Gauss(&t->rand);
      if (!flgdiag)
         
        for (i = 0; i < N; ++i) {
          for (j = 0, sum = 0.; j < N; ++j)
            sum += t->B[i][j] * t->rgdTmp[j];
          t->rgrgx[iNk][i] = xmean[i] + t->sigma * sum;
        }
    }
  if(t->state == 3 || t->gen == 0)
    ++t->gen;
  t->state = 1; 

  return(t->rgrgx);
}  

 
 
double const * 
cmaes_ReSampleSingle_old( cmaes_t *t, double *rgx)
{
  int i, j, N=t->sp.N;
  double sum; 

  if (rgx == NULL)
    FATAL("cmaes_ReSampleSingle(): Missing input double *x",0,0,0);

  for (i = 0; i < N; ++i)
    t->rgdTmp[i] = t->rgD[i] * cmaes_random_Gauss(&t->rand);
   
  for (i = 0; i < N; ++i) {
    for (j = 0, sum = 0.; j < N; ++j)
      sum += t->B[i][j] * t->rgdTmp[j];
    rgx[i] = t->rgxmean[i] + t->sigma * sum;
  }
  return rgx;
}

 
 
double * const * 
cmaes_ReSampleSingle( cmaes_t *t, int iindex)
{
  int i, j, N=t->sp.N;
  double *rgx; 
  double sum; 
  static char s[99];

  if (iindex < 0 || iindex >= t->sp.lambda) {
    sprintf(s, "index==%d must be between 0 and %d", iindex, t->sp.lambda);
    FATAL("cmaes_ReSampleSingle(): Population member ",s,0,0);
  }
  rgx = t->rgrgx[iindex];

  for (i = 0; i < N; ++i)
    t->rgdTmp[i] = t->rgD[i] * cmaes_random_Gauss(&t->rand);
   
  for (i = 0; i < N; ++i) {
    for (j = 0, sum = 0.; j < N; ++j)
      sum += t->B[i][j] * t->rgdTmp[j];
    rgx[i] = t->rgxmean[i] + t->sigma * sum;
  }
  return(t->rgrgx);
}

 
 
double * 
cmaes_SampleSingleInto( cmaes_t *t, double *rgx)
{
  int i, j, N=t->sp.N;
  double sum; 

  if (rgx == NULL)
    rgx = new_double(N);

  for (i = 0; i < N; ++i)
    t->rgdTmp[i] = t->rgD[i] * cmaes_random_Gauss(&t->rand);
   
  for (i = 0; i < N; ++i) {
    for (j = 0, sum = 0.; j < N; ++j)
      sum += t->B[i][j] * t->rgdTmp[j];
    rgx[i] = t->rgxmean[i] + t->sigma * sum;
  }
  return rgx;
}

 
 
double * 
cmaes_PerturbSolutionInto( cmaes_t *t, double *rgx, double const *xmean, double eps)
{
  int i, j, N=t->sp.N;
  double sum; 

  if (rgx == NULL)
    rgx = new_double(N);
  if (xmean == NULL)
    FATAL("cmaes_PerturbSolutionInto(): xmean was not given",0,0,0);

  for (i = 0; i < N; ++i)
    t->rgdTmp[i] = t->rgD[i] * cmaes_random_Gauss(&t->rand);
   
  for (i = 0; i < N; ++i) {
    for (j = 0, sum = 0.; j < N; ++j)
      sum += t->B[i][j] * t->rgdTmp[j];
    rgx[i] = xmean[i] + eps * t->sigma * sum;
  }
  return rgx;
}

 
 
const double *
cmaes_Optimize( cmaes_t *evo, double(*pFun)(double const *, int dim), long iterations)
 
{
    const char * signalsFilename = "cmaes_signals.par";
    double *const*pop;  
    const char *stop;
    int i;
    double startiter = evo->gen; 
    
    while(!(stop=cmaes_TestForTermination(evo)) && 
        (evo->gen < startiter + iterations || !iterations))
    { 
         
        pop = cmaes_SamplePopulation(evo);  

         
        for (i = 0; i < cmaes_Get(evo, "popsize"); ++i) {
            evo->publicFitness[i] = (*pFun)(pop[i], evo->sp.N); 
        }

         
        cmaes_UpdateDistribution(evo, evo->publicFitness); 

         
        if (signalsFilename)
            cmaes_ReadSignals(evo, signalsFilename); 
        fflush(stdout);
    }  

     
    cmaes_WriteToFile(evo, "all", "allcmaes.dat");

    return cmaes_GetPtr(evo, "xbestever");
}


 
 
double *
cmaes_UpdateDistribution( cmaes_t *t, const double *rgFunVal)
{
  int i, j, iNk, hsig, N=t->sp.N;
  int flgdiag = ((t->sp.diagonalCov == 1) || (t->sp.diagonalCov >= t->gen)); 
  double sum; 
  double psxps; 
  
  if(t->state == 3)
    FATAL("cmaes_UpdateDistribution(): You need to call \n",
          "SamplePopulation() before update can take place.",0,0);
  if(rgFunVal == NULL) 
    FATAL("cmaes_UpdateDistribution(): ", 
          "Fitness function value array input is missing.",0,0);

  if(t->state == 1)   
    t->countevals += t->sp.lambda;
  else
    ERRORMESSAGE("cmaes_UpdateDistribution(): unexpected state",0,0,0);

   
  for (i=0; i < t->sp.lambda; ++i) 
    t->rgrgx[i][N] = t->rgFuncValue[i] = rgFunVal[i];
  

   
  Sorted_index(rgFunVal, t->index, t->sp.lambda);
  
   
  if (t->rgFuncValue[t->index[0]] == 
      t->rgFuncValue[t->index[(int)t->sp.lambda/2]]) {
    t->sigma *= exp(0.2+t->sp.cs/t->sp.damps);
    ERRORMESSAGE("Warning: sigma increased due to equal function values\n",
                 "   Reconsider the formulation of the objective function",0,0);
  }
  
   
  for(i = (int)*(t->arFuncValueHist-1)-1; i > 0; --i)  
    t->arFuncValueHist[i] = t->arFuncValueHist[i-1];
  t->arFuncValueHist[0] = rgFunVal[t->index[0]];

   
  if (t->rgxbestever[N] > t->rgrgx[t->index[0]][N] || t->gen == 1)
    for (i = 0; i <= N; ++i) {
      t->rgxbestever[i] = t->rgrgx[t->index[0]][i];
      t->rgxbestever[N+1] = t->countevals;
    }

   
  for (i = 0; i < N; ++i) {
    t->rgxold[i] = t->rgxmean[i]; 
    t->rgxmean[i] = 0.;
    for (iNk = 0; iNk < t->sp.mu; ++iNk) 
      t->rgxmean[i] += t->sp.weights[iNk] * t->rgrgx[t->index[iNk]][i];
    t->rgBDz[i] = sqrt(t->sp.mueff)*(t->rgxmean[i] - t->rgxold[i])/t->sigma; 
  }

   
  for (i = 0; i < N; ++i) {
    if (!flgdiag)
      for (j = 0, sum = 0.; j < N; ++j)
        sum += t->B[j][i] * t->rgBDz[j];
    else
      sum = t->rgBDz[i];
    t->rgdTmp[i] = sum / t->rgD[i];
  }
  
   
  


  
  





 

   
  for (i = 0; i < N; ++i) {
    if (!flgdiag)
      for (j = 0, sum = 0.; j < N; ++j)
        sum += t->B[i][j] * t->rgdTmp[j];
    else
      sum = t->rgdTmp[i];
    t->rgps[i] = (1. - t->sp.cs) * t->rgps[i] + 
      sqrt(t->sp.cs * (2. - t->sp.cs)) * sum;
  }
  
   
  for (i = 0, psxps = 0.; i < N; ++i)
    psxps += t->rgps[i] * t->rgps[i];

   
  hsig = sqrt(psxps) / sqrt(1. - pow(1.-t->sp.cs, 2*t->gen)) / t->chiN
    < 1.4 + 2./(N+1);
  for (i = 0; i < N; ++i) {
    t->rgpc[i] = (1. - t->sp.ccumcov) * t->rgpc[i] + 
      hsig * sqrt(t->sp.ccumcov * (2. - t->sp.ccumcov)) * t->rgBDz[i];
  }
  
   
  if (t->flgIniphase && 
      t->gen > douMin(1/t->sp.cs, 1+N/t->sp.mucov)) 
    {
      if (psxps / t->sp.damps / (1.-pow((1. - t->sp.cs), t->gen)) 
          < N * 1.05) 
        t->flgIniphase = 0;
    }
  
#if 0
   
   
  if(psxps/N > 1.5 + 10.*sqrt(2./N) 
     && t->arFuncValueHist[0] > t->arFuncValueHist[1]
     && t->arFuncValueHist[0] > t->arFuncValueHist[2]) {
    double tfac = sqrt((1 + douMax(0, log(psxps/N))) * N / psxps);
    for (i=0; i<N; ++i) 
      t->rgps[i] *= tfac;
    psxps *= tfac*tfac; 
  }
#endif
  
   

  Adapt_C2(t, hsig);
  
   

#if 0
  if (t->sp.ccov != 0. && t->flgIniphase == 0) {
    int k; 

    t->flgEigensysIsUptodate = 0;

     
    for (i = 0; i < N; ++i)
      for (j = 0; j <=i; ++j) {
        t->C[i][j] = (1 - t->sp.ccov) * t->C[i][j] 
          + t->sp.ccov * (1./t->sp.mucov) 
            * (t->rgpc[i] * t->rgpc[j] 
               + (1-hsig)*t->sp.ccumcov*(2.-t->sp.ccumcov) * t->C[i][j]);
        for (k = 0; k < t->sp.mu; ++k)  
          t->C[i][j] += t->sp.ccov * (1-1./t->sp.mucov) * t->sp.weights[k]  
            * (t->rgrgx[t->index[k]][i] - t->rgxold[i]) 
            * (t->rgrgx[t->index[k]][j] - t->rgxold[j])
            / t->sigma / t->sigma; 
      }
  }
#endif


   
  t->sigma *= exp(((sqrt(psxps)/t->chiN)-1.)*t->sp.cs/t->sp.damps);

  t->state = 3;

  return (t->rgxmean);

}  


 
 
static void
Adapt_C2(cmaes_t *t, int hsig)
{
  int i, j, k, N=t->sp.N;
  int flgdiag = ((t->sp.diagonalCov == 1) || (t->sp.diagonalCov >= t->gen)); 

  if (t->sp.ccov != 0. && t->flgIniphase == 0) {

     
    double ccov1 = douMin(t->sp.ccov * (1./t->sp.mucov) * (flgdiag ? (N+1.5) / 3. : 1.), 1.);
    double ccovmu = douMin(t->sp.ccov * (1-1./t->sp.mucov)* (flgdiag ? (N+1.5) / 3. : 1.), 1.-ccov1); 
    double sigmasquare = t->sigma * t->sigma; 

    t->flgEigensysIsUptodate = 0;

     
    for (i = 0; i < N; ++i)
      for (j = flgdiag ? i : 0; j <= i; ++j) {
        t->C[i][j] = (1 - ccov1 - ccovmu) * t->C[i][j] 
          + ccov1
            * (t->rgpc[i] * t->rgpc[j] 
               + (1-hsig)*t->sp.ccumcov*(2.-t->sp.ccumcov) * t->C[i][j]);
        for (k = 0; k < t->sp.mu; ++k) {  
          t->C[i][j] += ccovmu * t->sp.weights[k]  
            * (t->rgrgx[t->index[k]][i] - t->rgxold[i]) 
            * (t->rgrgx[t->index[k]][j] - t->rgxold[j])
            / sigmasquare;
        }
      }
     
    t->maxdiagC = t->mindiagC = t->C[0][0];
    for (i = 1; i < N; ++i) {
      if (t->maxdiagC < t->C[i][i])
        t->maxdiagC = t->C[i][i];
      else if (t->mindiagC > t->C[i][i])
        t->mindiagC = t->C[i][i];
    }
  }  
}


 
 
static void 
TestMinStdDevs(cmaes_t *t)
   
{
  int i, N = t->sp.N; 
  if (t->sp.rgDiffMinChange == NULL)
    return;

  for (i = 0; i < N; ++i)
    while (t->sigma * sqrt(t->C[i][i]) < t->sp.rgDiffMinChange[i]) 
      t->sigma *= exp(0.05+t->sp.cs/t->sp.damps);

}  


 
 
void cmaes_WriteToFile(cmaes_t *t, const char *key, const char *name)
{ 
  cmaes_WriteToFileAW(t, key, name, "a");  
}

 
 
void cmaes_WriteToFileAW(cmaes_t *t, const char *key, const char *name, 
                         const char *appendwrite)
{ 
  const char *s = "tmpcmaes.dat"; 
  FILE *fp;
  
  if (name == NULL)
    name = s; 

  fp = fopen( name, appendwrite); 

  if(fp == NULL) {
    ERRORMESSAGE("cmaes_WriteToFile(): could not open '", name, 
                 "' with flag ", appendwrite);
    return;
  }

  if (appendwrite[0] == 'w') {
     
    fprintf(fp, "%% # %s (randomSeed=%d, %s)\n", key, t->sp.seed, getTimeStr());
  } else 
    if (t->gen > 0 || strncmp(name, "outcmaesfit", 11) != 0)
      cmaes_WriteToFilePtr(t, key, fp);  

  fclose(fp);

}  

 
void cmaes_WriteToFilePtr(cmaes_t *t, const char *key, FILE *fp)







 
{ 
  int i, k, N=(t ? t->sp.N : 0); 
  char const *keyend;  
  const char *s = "few";
  if (key == NULL)
    key = s; 
    
  keyend = key + strlen(key);

  while (key < keyend)
    {
      if (strncmp(key, "axisratio", 9) == 0)
        {
          fprintf(fp, "%.2e", sqrt(t->maxEW/t->minEW));
          while (*key != '+' && *key != '\0' && key < keyend)
           ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "idxminSD", 8) == 0)
        {
          int mini=0; for(i=N-1;i>0;--i) if(t->mindiagC==t->C[i][i]) mini=i; 
          fprintf(fp, "%d", mini+1);
          while (*key != '+' && *key != '\0' && key < keyend)
           ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "idxmaxSD", 8) == 0)
        {
          int maxi=0; for(i=N-1;i>0;--i) if(t->maxdiagC==t->C[i][i]) maxi=i; 
          fprintf(fp, "%d", maxi+1);
          while (*key != '+' && *key != '\0' && key < keyend)
           ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
       
      if (strncmp(key, "B", 1) == 0) 
        {
           
          int j, *iindex=(int*)(new_void(N,sizeof(int)));  
          Sorted_index(t->rgD, iindex, N);  
           
          for (i = 0; i < N; ++i)
            for (j = 0; j < N; ++j)
              fprintf(fp, "%g%c", t->B[j][iindex[N-1-i]], (j==N-1)?'\n':'\t');
          ++key; 
          free(iindex);  
        }
       
      if (strncmp(key, "C", 1) == 0) 
        {
          int j;
          for (i = 0; i < N; ++i)
            for (j = 0; j <= i; ++j)
              fprintf(fp, "%g%c", t->C[i][j], (j==i)?'\n':'\t');
          ++key; 
        }
        
      if (strncmp(key, "clock", 4) == 0)
        {
          cmaes_timings_update(&t->eigenTimings);
          fprintf(fp, "%.1f %.1f",  t->eigenTimings.totaltotaltime, 
                  t->eigenTimings.tictoctime);
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
       
      if (strncmp(key, "stddevratio", 11) == 0)  
        {
          fprintf(fp, "%g", sqrt(t->maxdiagC/t->mindiagC));
          while (*key != '+' && *key != '\0' && key < keyend)
           ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
       
      if (strncmp(key, "coorstddev", 10) == 0 
          || strncmp(key, "stddev", 6) == 0)  
        {
          for (i = 0; i < N; ++i)
            fprintf(fp, "%s%g", (i==0) ? "":"\t", t->sigma*sqrt(t->C[i][i]));
          while (*key != '+' && *key != '\0' && key < keyend)
           ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
       
      if (strncmp(key, "diag(D)", 7) == 0)
        {
          for (i = 0; i < N; ++i)
            t->rgdTmp[i] = t->rgD[i];
          qsort(t->rgdTmp, (unsigned) N, sizeof(double), &SignOfDiff);  
          for (i = 0; i < N; ++i)
            fprintf(fp, "%s%g", (i==0) ? "":"\t", t->rgdTmp[i]);
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "dim", 3) == 0)
        {
          fprintf(fp, "%d", N);
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "eval", 4) == 0)
        {
          fprintf(fp, "%.0f", t->countevals);
          while (*key != '+' && *key != '\0' && key < keyend)
           ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "few(diag(D))", 12) == 0) 
        { 
          int add = (int)(0.5 + (N + 1.) / 5.); 
          for (i = 0; i < N; ++i)
            t->rgdTmp[i] = t->rgD[i];
          qsort(t->rgdTmp, (unsigned) N, sizeof(double), &SignOfDiff);
          for (i = 0; i < N-1; i+=add)         
            fprintf(fp, "%s%g", (i==0) ? "":"\t", t->rgdTmp[N-1-i]);
          fprintf(fp, "\t%g\n", t->rgdTmp[0]);         
          break;  
        }
      if (strncmp(key, "fewinfo", 7) == 0) { 
        fprintf(fp," Iter Fevals  Function Value         Sigma   ");
        fprintf(fp, "MaxCoorDev MinCoorDev AxisRatio   MinDii   Time in eig\n");
        while (*key != '+' && *key != '\0' && key < keyend)
          ++key;
      }
      if (strncmp(key, "few", 3) == 0) { 
        fprintf(fp, " %4.0f ", t->gen); 
        fprintf(fp, " %5.0f ", t->countevals); 
        fprintf(fp, "%.15e", t->rgFuncValue[t->index[0]]);
        fprintf(fp, "  %.2e  %.2e %.2e", t->sigma, t->sigma*sqrt(t->maxdiagC), 
                t->sigma*sqrt(t->mindiagC));
        fprintf(fp, "  %.2e  %.2e", sqrt(t->maxEW/t->minEW), sqrt(t->minEW));
        while (*key != '+' && *key != '\0' && key < keyend)
          ++key;
        fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
      }
      if (strncmp(key, "funval", 6) == 0 || strncmp(key, "fitness", 6) == 0)
        {
          fprintf(fp, "%.15e", t->rgFuncValue[t->index[0]]);
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "fbestever", 9) == 0)
        {
          fprintf(fp, "%.15e", t->rgxbestever[N]);  
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "fmedian", 7) == 0)
        {
          fprintf(fp, "%.15e", t->rgFuncValue[t->index[(int)(t->sp.lambda/2)]]);
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "fworst", 6) == 0)
        {
          fprintf(fp, "%.15e", t->rgFuncValue[t->index[t->sp.lambda-1]]);
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "arfunval", 8) == 0 || strncmp(key, "arfitness", 8) == 0)
        {
          for (i = 0; i < N; ++i)
            fprintf(fp, "%s%.10e", (i==0) ? "" : "\t", 
                    t->rgFuncValue[t->index[i]]);
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "gen", 3) == 0)
        {
          fprintf(fp, "%.0f", t->gen);
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "iter", 4) == 0)
        {
          fprintf(fp, "%.0f", t->gen);
          while (*key != '+' && *key != '\0' && key < keyend)
           ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "sigma", 5) == 0)
        {
          fprintf(fp, "%.4e", t->sigma);
          while (*key != '+' && *key != '\0' && key < keyend)
           ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "minSD", 5) == 0)  
        {
          fprintf(fp, "%.4e", sqrt(t->mindiagC));
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "maxSD", 5) == 0)
        {
          fprintf(fp, "%.4e", sqrt(t->maxdiagC));
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "mindii", 6) == 0)
        {
          fprintf(fp, "%.4e", sqrt(t->minEW));
          while (*key != '+' && *key != '\0' && key < keyend)
           ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "0", 1) == 0)
        {
          fprintf(fp, "0");
          ++key; 
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "lambda", 6) == 0 || strncmp(key, "popsi", 5) == 0 || strncmp(key, "populationsi", 12) == 0)
        {
          fprintf(fp, "%d", t->sp.lambda);
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "N", 1) == 0)
        {
          fprintf(fp, "%d", N);
          ++key; 
          fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
        }
      if (strncmp(key, "resume", 6) == 0)
        {
          fprintf(fp, "\n# resume %d\n", N);
          fprintf(fp, "xmean\n");
          cmaes_WriteToFilePtr(t, "xmean", fp);
          fprintf(fp, "path for sigma\n");
          for(i=0; i<N; ++i)
            fprintf(fp, "%g%s", t->rgps[i], (i==N-1) ? "\n":"\t");
          fprintf(fp, "path for C\n");
          for(i=0; i<N; ++i)
            fprintf(fp, "%g%s", t->rgpc[i], (i==N-1) ? "\n":"\t");
          fprintf(fp, "sigma %g\n", t->sigma);
           
          fprintf(fp, "covariance matrix\n"); 
          cmaes_WriteToFilePtr(t, "C", fp);
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
        }
      if (strncmp(key, "xbest", 5) == 0) {  
        for(i=0; i<N; ++i)
          fprintf(fp, "%s%g", (i==0) ? "":"\t", t->rgrgx[t->index[0]][i]);
        while (*key != '+' && *key != '\0' && key < keyend)
          ++key;
        fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
      }
      if (strncmp(key, "xmean", 5) == 0) {
        for(i=0; i<N; ++i)
          fprintf(fp, "%s%g", (i==0) ? "":"\t", t->rgxmean[i]);
        while (*key != '+' && *key != '\0' && key < keyend)
          ++key;
        fprintf(fp, "%c", (*key=='+') ? '\t':'\n');
      }
      if (strncmp(key, "all", 3) == 0)
        {
          time_t ti = time(NULL);
          fprintf(fp, "\n# --------- %s\n", asctime(localtime(&ti)));
          fprintf(fp, " N %d\n", N);
          fprintf(fp, " seed %d\n", t->sp.seed);
          fprintf(fp, "function evaluations %.0f\n", t->countevals);
          fprintf(fp, "elapsed (CPU) time [s] %.2f\n", t->eigenTimings.totaltotaltime);
          fprintf(fp, "function value f(x)=%g\n", t->rgrgx[t->index[0]][N]);
          fprintf(fp, "maximal standard deviation %g\n", t->sigma*sqrt(t->maxdiagC));
          fprintf(fp, "minimal standard deviation %g\n", t->sigma*sqrt(t->mindiagC));
          fprintf(fp, "sigma %g\n", t->sigma);
          fprintf(fp, "axisratio %g\n", rgdouMax(t->rgD, N)/rgdouMin(t->rgD, N));
          fprintf(fp, "xbestever found after %.0f evaluations, function value %g\n", 
                  t->rgxbestever[N+1], t->rgxbestever[N]);
          for(i=0; i<N; ++i)
            fprintf(fp, " %12g%c", t->rgxbestever[i], 
                    (i%5==4||i==N-1)?'\n':' ');
          fprintf(fp, "xbest (of last generation, function value %g)\n", 
                  t->rgrgx[t->index[0]][N]); 
          for(i=0; i<N; ++i)
            fprintf(fp, " %12g%c", t->rgrgx[t->index[0]][i], 
                    (i%5==4||i==N-1)?'\n':' ');
          fprintf(fp, "xmean \n");
          for(i=0; i<N; ++i)
            fprintf(fp, " %12g%c", t->rgxmean[i], 
                    (i%5==4||i==N-1)?'\n':' ');
          fprintf(fp, "Standard deviation of coordinate axes (sigma*sqrt(diag(C)))\n");
          for(i=0; i<N; ++i)
            fprintf(fp, " %12g%c", t->sigma*sqrt(t->C[i][i]), 
                    (i%5==4||i==N-1)?'\n':' ');
          fprintf(fp, "Main axis lengths of mutation ellipsoid (sigma*diag(D))\n");
          for (i = 0; i < N; ++i)
            t->rgdTmp[i] = t->rgD[i];
          qsort(t->rgdTmp, (unsigned) N, sizeof(double), &SignOfDiff);
          for(i=0; i<N; ++i)
            fprintf(fp, " %12g%c", t->sigma*t->rgdTmp[N-1-i], 
                    (i%5==4||i==N-1)?'\n':' ');
          fprintf(fp, "Longest axis (b_i where d_ii=max(diag(D))\n");
          k = MaxIdx(t->rgD, N);
          for(i=0; i<N; ++i)
            fprintf(fp, " %12g%c", t->B[i][k], (i%5==4||i==N-1)?'\n':' ');
          fprintf(fp, "Shortest axis (b_i where d_ii=max(diag(D))\n");
          k = MinIdx(t->rgD, N);
          for(i=0; i<N; ++i)
            fprintf(fp, " %12g%c", t->B[i][k], (i%5==4||i==N-1)?'\n':' ');
          while (*key != '+' && *key != '\0' && key < keyend)
            ++key;
        }  

#if 0  
      s0 = key;
      d = cmaes_Get(t, key);  
      if (key == s0)  
        {
           
           
        }
#endif 

      if (*key == '\0') 
        break; 
      else if (*key != '+') {  
        ERRORMESSAGE("cmaes_t:WriteToFilePtr(): unrecognized key '", key, "'", 0); 
        while (*key != '+' && *key != '\0' && key < keyend)
          ++key;
      }
      while (*key == '+') 
        ++key; 
    }   

  if (key > keyend)
    FATAL("cmaes_t:WriteToFilePtr(): BUG regarding key sequence",0,0,0);

}  

 
double  
cmaes_Get( cmaes_t *t, char const *s)
{
  int N=t->sp.N;

  if (strncmp(s, "axisratio", 5) == 0) {  
    return (rgdouMax(t->rgD, N)/rgdouMin(t->rgD, N));
  }
  else if (strncmp(s, "eval", 4) == 0) {  
    return (t->countevals);
  }
  else if (strncmp(s, "fctvalue", 6) == 0
           || strncmp(s, "funcvalue", 6) == 0
           || strncmp(s, "funvalue", 6) == 0
           || strncmp(s, "fitness", 3) == 0) {  
    return(t->rgFuncValue[t->index[0]]);
  }
  else if (strncmp(s, "fbestever", 7) == 0) {  
    return(t->rgxbestever[N]);
  }
  else if (strncmp(s, "generation", 3) == 0
           || strncmp(s, "iteration", 4) == 0) { 
    return(t->gen);
  }
  else if (strncmp(s, "maxeval", 4) == 0
           || strncmp(s, "MaxFunEvals", 8) == 0
           || strncmp(s, "stopMaxFunEvals", 12) == 0) {  
    return(t->sp.stopMaxFunEvals);
  }
  else if (strncmp(s, "maxgen", 4) == 0
           || strncmp(s, "MaxIter", 7) == 0
           || strncmp(s, "stopMaxIter", 11) == 0) {  
    return(ceil(t->sp.stopMaxIter));
  }
  else if (strncmp(s, "maxaxislength", 5) == 0) {  
    return(t->sigma * sqrt(t->maxEW));
  }
  else if (strncmp(s, "minaxislength", 5) == 0) {  
    return(t->sigma * sqrt(t->minEW));
  }
  else if (strncmp(s, "maxstddev", 4) == 0) {  
    return(t->sigma * sqrt(t->maxdiagC));
  }
  else if (strncmp(s, "minstddev", 4) == 0) {  
    return(t->sigma * sqrt(t->mindiagC));
  }
  else if (strncmp(s, "N", 1) == 0 || strcmp(s, "n") == 0 || 
           strncmp(s, "dimension", 3) == 0) {
    return (N);
  }
  else if (strncmp(s, "lambda", 3) == 0
           || strncmp(s, "samplesize", 8) == 0
           || strncmp(s, "popsize", 7) == 0) {  
    return(t->sp.lambda);
  }
  else if (strncmp(s, "sigma", 3) == 0) {
    return(t->sigma);
  }
  FATAL( "cmaes_Get(cmaes_t, char const * s): No match found for s='", s, "'",0);
  return(0);
}  

 
double * 
cmaes_GetInto( cmaes_t *t, char const *s, double *res)
{
  int i, N = t->sp.N;
  double const * res0 = cmaes_GetPtr(t, s);
  if (res == NULL)
    res = new_double(N);
  for (i = 0; i < N; ++i)
    res[i] = res0[i];
  return res; 
}

 
double * 
cmaes_GetNew( cmaes_t *t, char const *s)
{
  return (cmaes_GetInto(t, s, NULL));
}

 
const double * 
cmaes_GetPtr( cmaes_t *t, char const *s)
{
  int i, N=t->sp.N;

   
  if (strncmp(s, "diag(C)", 7) == 0) { 
    for (i = 0; i < N; ++i)
      t->rgout[i] = t->C[i][i]; 
    return(t->rgout);
  }
   
  else if (strncmp(s, "diag(D)", 7) == 0) { 
    return(t->rgD);
  }
   
  else if (strncmp(s, "stddev", 3) == 0) { 
    for (i = 0; i < N; ++i)
      t->rgout[i] = t->sigma * sqrt(t->C[i][i]); 
    return(t->rgout);
  }
   
  else if (strncmp(s, "xbestever", 7) == 0)
    return(t->rgxbestever);
   
  else if (strncmp(s, "xbest", 5) == 0)
    return(t->rgrgx[t->index[0]]);
   
  else if (strncmp(s, "xmean", 1) == 0)
    return(t->rgxmean);
  
  return(NULL);
}

 



 
const char *
cmaes_TestForTermination( cmaes_t *t)
{
  double range, fac;
  int iAchse, iKoo;
  int flgdiag = ((t->sp.diagonalCov == 1) || (t->sp.diagonalCov >= t->gen)); 
  static char sTestOutString[3024];
  char * cp = sTestOutString;
  int i, cTemp, N=t->sp.N; 
  cp[0] = '\0';

       
      if ((t->gen > 1 || t->state > 1) && t->sp.stStopFitness.flg && 
          t->rgFuncValue[t->index[0]] <= t->sp.stStopFitness.val) 
        cp += sprintf(cp, "Fitness: function value %7.2e <= stopFitness (%7.2e)\n", 
                      t->rgFuncValue[t->index[0]], t->sp.stStopFitness.val);
      
       
      range = douMax(rgdouMax(t->arFuncValueHist, (int)douMin(t->gen,*(t->arFuncValueHist-1))), 
                     rgdouMax(t->rgFuncValue, t->sp.lambda)) -
        douMin(rgdouMin(t->arFuncValueHist, (int)douMin(t->gen, *(t->arFuncValueHist-1))), 
               rgdouMin(t->rgFuncValue, t->sp.lambda));
      
      if (t->gen > 0 && range <= t->sp.stopTolFun) {
        cp += sprintf(cp, 
                      "TolFun: function value differences %7.2e < stopTolFun=%7.2e\n", 
                      range, t->sp.stopTolFun);
      }

       
      if (t->gen > *(t->arFuncValueHist-1)) {
        range = rgdouMax(t->arFuncValueHist, (int)*(t->arFuncValueHist-1)) 
          - rgdouMin(t->arFuncValueHist, (int)*(t->arFuncValueHist-1));
        if (range <= t->sp.stopTolFunHist)
          cp += sprintf(cp, 
                        "TolFunHist: history of function value changes %7.2e stopTolFunHist=%7.2e", 
                        range, t->sp.stopTolFunHist);
      }
      
       
      for(i=0, cTemp=0; i<N; ++i) {
        cTemp += (t->sigma * sqrt(t->C[i][i]) < t->sp.stopTolX) ? 1 : 0;
        cTemp += (t->sigma * t->rgpc[i] < t->sp.stopTolX) ? 1 : 0;
      }
      if (cTemp == 2*N) {
        cp += sprintf(cp, 
                      "TolX: object variable changes below %7.2e \n", 
                      t->sp.stopTolX);
      }

       
      for(i=0; i<N; ++i) {
        if (t->sigma * sqrt(t->C[i][i]) > t->sp.stopTolUpXFactor * t->sp.rgInitialStds[i])
          break;
      }
      if (i < N) {
        cp += sprintf(cp, 
                      "TolUpX: standard deviation increased by more than %7.2e, larger initial standard deviation recommended \n", 
                      t->sp.stopTolUpXFactor);
      }

       
      if (t->maxEW >= t->minEW * t->dMaxSignifKond) {
        cp += sprintf(cp, 
                      "ConditionNumber: maximal condition number %7.2e reached. maxEW=%7.2e,minEW=%7.2e,maxdiagC=%7.2e,mindiagC=%7.2e\n", 
                      t->dMaxSignifKond, t->maxEW, t->minEW, t->maxdiagC, t->mindiagC);
      }  
      
      
 
      if (!flgdiag) {
        for (iAchse = 0; iAchse < N; ++iAchse)
          {
            fac = 0.1 * t->sigma * t->rgD[iAchse];
            for (iKoo = 0; iKoo < N; ++iKoo){ 
              if (t->rgxmean[iKoo] != t->rgxmean[iKoo] + fac * t->B[iKoo][iAchse])
                break;
            }
            if (iKoo == N)        
              {
                 
                cp += sprintf(cp, 
                              "NoEffectAxis: standard deviation 0.1*%7.2e in principal axis %d without effect\n", 
                              fac/0.1, iAchse);
                break;
              }  
          }  
      }  
       
      for (iKoo = 0; iKoo < N; ++iKoo)
        {
          if (t->rgxmean[iKoo] == t->rgxmean[iKoo] + 
              0.2*t->sigma*sqrt(t->C[iKoo][iKoo]))
            {
               
               
              cp += sprintf(cp, 
                            "NoEffectCoordinate: standard deviation 0.2*%7.2e in coordinate %d without effect\n", 
                            t->sigma*sqrt(t->C[iKoo][iKoo]), iKoo); 
              break;
            }
          
        }  
       

      if(t->countevals >= t->sp.stopMaxFunEvals) 
        cp += sprintf(cp, "MaxFunEvals: conducted function evaluations %.0f >= %g\n", 
                      t->countevals, t->sp.stopMaxFunEvals);
      if(t->gen >= t->sp.stopMaxIter) 
        cp += sprintf(cp, "MaxIter: number of iterations %.0f >= %g\n", 
                      t->gen, t->sp.stopMaxIter); 
      if(t->flgStop)
        cp += sprintf(cp, "Manual: stop signal read\n");

#if 0
  else if (0) {
    for(i=0, cTemp=0; i<N; ++i) {
      cTemp += (sigma * sqrt(C[i][i]) < stopdx) ? 1 : 0;
      cTemp += (sigma * rgpc[i] < stopdx) ? 1 : 0;
    }
    if (cTemp == 2*N)
      flgStop = 1;
  }
#endif

  if (cp - sTestOutString>320)
    ERRORMESSAGE("Bug in cmaes_t:Test(): sTestOutString too short",0,0,0);

  if (cp != sTestOutString) {
    return sTestOutString;
  }

  return(NULL);
  
}  

 
void cmaes_ReadSignals(cmaes_t *t, char const *filename)
{
  const char *s = "cmaes_signals.par";
  FILE *fp;
  if (filename == NULL)
    filename = s; 
 
  fp = fopen( filename, "r"); 
  if(fp == NULL) {
    return;
  }
  cmaes_ReadFromFilePtr( t, fp);
  fclose(fp);
}
 
void cmaes_ReadFromFilePtr( cmaes_t *t, FILE *fp)

 
{
  const char *keys[15];  
  char s[199], sin1[99], sin2[129], sin3[99], sin4[99];
  int ikey, ckeys, nb; 
  double d; 
  static int flglockprint = 0;
  static int flglockwrite = 0;
  static long countiterlastwritten; 
  static long maxdiffitertowrite;  
  int flgprinted = 0;
  int flgwritten = 0; 
  double deltaprinttime = (double)(time(NULL)-t->printtime);  
  double deltawritetime = (double)(time(NULL)-t->writetime);  
  double deltaprinttimefirst = (double)(t->firstprinttime ? time(NULL)-t->firstprinttime : 0);  
  double deltawritetimefirst = (double)(t->firstwritetime ? time(NULL)-t->firstwritetime : 0); 
  if (countiterlastwritten > t->gen) {  
    maxdiffitertowrite = 0;
    countiterlastwritten = 0;
  }

  keys[0] = " stop%98s %98s";         
                                      
  keys[1] = " print %98s %98s";        
  keys[2] = " write %98s %128s %98s";  
  keys[3] = " check%98s %98s";
  keys[4] = " maxTimeFractionForEigendecompostion %98s";
  ckeys = 5; 
  strcpy(sin2, "tmpcmaes.dat");

  if (cmaes_TestForTermination(t)) 
    {
      deltaprinttime = (double)time(NULL);  
      deltawritetime = (double)time(NULL);
    }
  while(fgets(s, sizeof(s), fp) != NULL)
    { 
      if (s[0] == '#' || s[0] == '%')  
        continue; 
      sin1[0] = sin2[0] = sin3[0] = sin4[0] = '\0';
      for (ikey=0; ikey < ckeys; ++ikey)
        {
          if((nb=sscanf(s, keys[ikey], sin1, sin2, sin3, sin4)) >= 1) 
            {
              switch(ikey) {
              case 0 :  
                if (strncmp(sin1, "now", 3) == 0) 
                  t->flgStop = 1; 
                else if (strncmp(sin1, "MaxFunEvals", 11) == 0) {
                  if (sscanf(sin2, " %lg", &d) == 1) 
                    t->sp.stopMaxFunEvals = d; 
                }
                else if (strncmp(sin1, "MaxIter", 4) == 0) {
                  if (sscanf(sin2, " %lg", &d) == 1) 
                    t->sp.stopMaxIter = d; 
                }
                else if (strncmp(sin1, "Fitness", 7) == 0) {
                  if (sscanf(sin2, " %lg", &d) == 1) 
                    {
                      t->sp.stStopFitness.flg = 1; 
                      t->sp.stStopFitness.val = d; 
                    }
                }
                else if (strncmp(sin1, "TolFunHist", 10) == 0) {
                  if (sscanf(sin2, " %lg", &d) == 1) 
                    t->sp.stopTolFunHist = d; 
                }
                else if (strncmp(sin1, "TolFun", 6) == 0) {
                  if (sscanf(sin2, " %lg", &d) == 1) 
                    t->sp.stopTolFun = d; 
                }
                else if (strncmp(sin1, "TolX", 4) == 0) {
                  if (sscanf(sin2, " %lg", &d) == 1) 
                    t->sp.stopTolX = d; 
                }
                else if (strncmp(sin1, "TolUpXFactor", 4) == 0) {
                  if (sscanf(sin2, " %lg", &d) == 1) 
                    t->sp.stopTolUpXFactor = d; 
                }
                break;
              case 1 :  
                d = 1;  
                if (sscanf(sin2, "%lg", &d) < 1 && deltaprinttimefirst < 1)
                  d = 0;  
                if (deltaprinttime >= d && !flglockprint) {
                  cmaes_WriteToFilePtr(t, sin1, stdout);
                  flgprinted = 1;
                }
                if(d < 0) 
                  flglockprint += 2;
                break; 
              case 2 :  
                 
                if (t->countevals < t->sp.lambda && t->flgresumedone == 0) 
                  cmaes_WriteToFileAW(t, sin1, sin2, "w");  
                d = 0.9;  
                if (sscanf(sin3, "%lg", &d) < 1 && deltawritetimefirst < 2)
                  d = 0;  
                if(d < 0) 
                  flglockwrite += 2;
                if (!flglockwrite) {
                  if (deltawritetime >= d) {
                    cmaes_WriteToFile(t, sin1, sin2);
                    flgwritten = 1; 
                  } else if (d < 1 
                             && t->gen-countiterlastwritten > maxdiffitertowrite) {
                    cmaes_WriteToFile(t, sin1, sin2);
                    flgwritten = 1; 
                  }
                }
                break; 
              case 3 :  
                if (strncmp(sin1, "eigen", 5) == 0) {
                  if (sscanf(sin2, " %lg", &d) == 1) {
                    if (d > 0)
                      t->flgCheckEigen = 1;
                    else
                      t->flgCheckEigen = 0;
                  }
                  else
                    t->flgCheckEigen = 0;
                }
                break;
              case 4 :  
                if (sscanf(sin1, " %lg", &d) == 1) 
                  t->sp.updateCmode.maxtime = d;
                break; 
              default :
                break; 
              }
              break;  
            }  
        }  
    }  
  if (t->writetime == 0) 
    t->firstwritetime = time(NULL); 
  if (t->printtime == 0)
    t->firstprinttime = time(NULL); 

  if (flgprinted)
    t->printtime = time(NULL);
  if (flgwritten) {
    t->writetime = time(NULL);
    if (t->gen-countiterlastwritten > maxdiffitertowrite)
      ++maxdiffitertowrite;  
    countiterlastwritten = (long int) t->gen;
  }
  --flglockprint;
  --flglockwrite;
  flglockprint = (flglockprint > 0) ? 1 : 0;
  flglockwrite = (flglockwrite > 0) ? 1 : 0;
}   

 
static int
Check_Eigen( int N,  double **C, double *diag, double **Q) 






 
{
     
  int i, j, k, res = 0;
  double cc, dd; 
  static char s[324];

  for (i=0; i < N; ++i)
    for (j=0; j < N; ++j) {
      for (cc=0.,dd=0., k=0; k < N; ++k) {
        cc += diag[k] * Q[i][k] * Q[j][k];
        dd += Q[i][k] * Q[j][k];
      }
       
      if (fabs(cc - C[i>j?i:j][i>j?j:i])/sqrt(C[i][i]*C[j][j]) > 1e-10 
          && fabs(cc - C[i>j?i:j][i>j?j:i]) > 3e-14) {
        sprintf(s, "%d %d: %.17e %.17e, %e", 
                i, j, cc, C[i>j?i:j][i>j?j:i], cc-C[i>j?i:j][i>j?j:i]);
        ERRORMESSAGE("cmaes_t:Eigen(): imprecise result detected ", 
                     s, 0, 0);
        ++res; 
      }
      if (fabs(dd - (i==j)) > 1e-10) {
        sprintf(s, "%d %d %.17e ", i, j, dd);
        ERRORMESSAGE("cmaes_t:Eigen(): imprecise result detected (Q not orthog.)", 
                     s, 0, 0);
        ++res;
      }
    }
  return res; 
}

 
 
void 
cmaes_UpdateEigensystem(cmaes_t *t, int flgforce)
{
  int i, N = t->sp.N;

  cmaes_timings_update(&t->eigenTimings);

  if(flgforce == 0) {
    if (t->flgEigensysIsUptodate == 1)
      return; 

      
    if (t->sp.updateCmode.flgalways == 0  
        && t->gen < t->genOfEigensysUpdate + t->sp.updateCmode.modulo
        )
      return;

     
    if (t->sp.updateCmode.maxtime < 1.00 
        && t->eigenTimings.tictoctime > t->sp.updateCmode.maxtime * t->eigenTimings.totaltime
        && t->eigenTimings.tictoctime > 0.0002)
      return; 
  }
  cmaes_timings_tic(&t->eigenTimings);

  Eigen( N, t->C, t->rgD, t->B, t->rgdTmp);
      
  cmaes_timings_toc(&t->eigenTimings);

   
  t->minEW = rgdouMin(t->rgD, N);
  t->maxEW = rgdouMax(t->rgD, N);

  if (t->flgCheckEigen)
      
    i = Check_Eigen( N, t->C, t->rgD, t->B);
  
#if 0 
   
  if (t->maxEW > t->minEW * t->dMaxSignifKond) {
    ERRORMESSAGE("Warning: Condition number of covariance matrix at upper limit.",
                 " Consider a rescaling or redesign of the objective function. " ,"","");
    printf("\nWarning: Condition number of covariance matrix at upper limit\n");
    tmp = t->maxEW/t->dMaxSignifKond - t->minEW;
    tmp = t->maxEW/t->dMaxSignifKond;
    t->minEW += tmp;
    for (i=0;i<N;++i) {
      t->C[i][i] += tmp;
      t->rgD[i] += tmp;
    }
  }  
  t->dLastMinEWgroesserNull = minEW;
#endif   
  
  for (i = 0; i < N; ++i)
    t->rgD[i] = sqrt(t->rgD[i]);
  
  t->flgEigensysIsUptodate = 1;
  t->genOfEigensysUpdate = t->gen; 
  
  return;
      
}  


 
static void 
Eigen( int N,  double **C, double *diag, double **Q, double *rgtmp)










 
{
  int i, j;

  if (rgtmp == NULL)  
    FATAL("cmaes_t:Eigen(): input parameter double *rgtmp must be non-NULL", 0,0,0);

   
  if (C != Q) {
    for (i=0; i < N; ++i)
      for (j = 0; j <= i; ++j)
        Q[i][j] = Q[j][i] = C[i][j];
  }

#if 0
    Householder( N, Q, diag, rgtmp);
    QLalgo( N, diag, Q, 30*N, rgtmp+1);
#else
    Householder2( N, Q, diag, rgtmp);
    QLalgo2( N, diag, rgtmp, Q);
#endif

}  


 
static void 
QLalgo2 (int n, double *d, double *e, double **V) {
  












 

  int i, k, l, m;
  double f = 0.0;
  double tst1 = 0.0;
  double eps = 2.22e-16;  
  
       
      for (i = 1; i < n; i++) {
         e[i-1] = e[i];
      }
      e[n-1] = 0.0;  
   
      for (l = 0; l < n; l++) { 

         
   
         if (tst1 < fabs(d[l]) + fabs(e[l]))
           tst1 = fabs(d[l]) + fabs(e[l]);
         m = l;
         while (m < n) {
           if (fabs(e[m]) <= eps*tst1) {
              
               break;
            }
            m++;
         }
   
          
          
   
         if (m > l) {   
            int iter = 0;
            do {  
               double dl1, h;
               double g = d[l];
               double p = (d[l+1] - g) / (2.0 * e[l]); 
               double r = myhypot(p, 1.); 

               iter = iter + 1;   
   
                
   
               if (p < 0) {
                  r = -r;
               }
               d[l] = e[l] / (p + r);
               d[l+1] = e[l] * (p + r);
               dl1 = d[l+1];
               h = g - d[l];
               for (i = l+2; i < n; i++) {
                  d[i] -= h;
               }
               f = f + h;
   
                
   
               p = d[m];
             {
               double c = 1.0;
               double c2 = c;
               double c3 = c;
               double el1 = e[l+1];
               double s = 0.0;
               double s2 = 0.0;
               for (i = m-1; i >= l; i--) {
                  c3 = c2;
                  c2 = c;
                  s2 = s;
                  g = c * e[i];
                  h = c * p;
                  r = myhypot(p, e[i]);
                  e[i+1] = s * r;
                  s = e[i] / r;
                  c = p / r;
                  p = c * d[i] - s * g;
                  d[i+1] = h + s * (c * g + s * d[i]);
   
                   
   
                  for (k = 0; k < n; k++) {
                     h = V[k][i+1];
                     V[k][i+1] = s * V[k][i] + c * h;
                     V[k][i] = c * V[k][i] - s * h;
                  }
               }
               p = -s * s2 * c3 * el1 * e[l] / dl1;
               e[l] = s * p;
               d[l] = c * p;
             }
   
                
   
            } while (fabs(e[l]) > eps*tst1);
         }
         d[l] = d[l] + f;
         e[l] = 0.0;
      }
     
       
#if 1
       
      {
      int j; 
      double p;
      for (i = 0; i < n-1; i++) {
         k = i;
         p = d[i];
         for (j = i+1; j < n; j++) {
            if (d[j] < p) {
               k = j;
               p = d[j];
            }
         }
         if (k != i) {
            d[k] = d[i];
            d[i] = p;
            for (j = 0; j < n; j++) {
               p = V[j][i];
               V[j][i] = V[j][k];
               V[j][k] = p;
            }
         }
      }
      }
#endif 
}   


 
static void 
Householder2(int n, double **V, double *d, double *e) {
  










 

  int i,j,k; 

      for (j = 0; j < n; j++) {
         d[j] = V[n-1][j];
      }

       
   
      for (i = n-1; i > 0; i--) {
   
         
   
         double scale = 0.0;
         double h = 0.0;
         for (k = 0; k < i; k++) {
            scale = scale + fabs(d[k]);
         }
         if (scale == 0.0) {
            e[i] = d[i-1];
            for (j = 0; j < i; j++) {
               d[j] = V[i-1][j];
               V[i][j] = 0.0;
               V[j][i] = 0.0;
            }
         } else {
   
            

            double f, g, hh;
   
            for (k = 0; k < i; k++) {
               d[k] /= scale;
               h += d[k] * d[k];
            }
            f = d[i-1];
            g = sqrt(h);
            if (f > 0) {
               g = -g;
            }
            e[i] = scale * g;
            h = h - f * g;
            d[i-1] = f - g;
            for (j = 0; j < i; j++) {
               e[j] = 0.0;
            }
   
             
   
            for (j = 0; j < i; j++) {
               f = d[j];
               V[j][i] = f;
               g = e[j] + V[j][j] * f;
               for (k = j+1; k <= i-1; k++) {
                  g += V[k][j] * d[k];
                  e[k] += V[k][j] * f;
               }
               e[j] = g;
            }
            f = 0.0;
            for (j = 0; j < i; j++) {
               e[j] /= h;
               f += e[j] * d[j];
            }
            hh = f / (h + h);
            for (j = 0; j < i; j++) {
               e[j] -= hh * d[j];
            }
            for (j = 0; j < i; j++) {
               f = d[j];
               g = e[j];
               for (k = j; k <= i-1; k++) {
                  V[k][j] -= (f * e[k] + g * d[k]);
               }
               d[j] = V[i-1][j];
               V[i][j] = 0.0;
            }
         }
         d[i] = h;
      }
   
       
   
      for (i = 0; i < n-1; i++) {
         double h; 
         V[n-1][i] = V[i][i];
         V[i][i] = 1.0;
         h = d[i+1];
         if (h != 0.0) {
            for (k = 0; k <= i; k++) {
               d[k] = V[k][i+1] / h;
            }
            for (j = 0; j <= i; j++) {
               double g = 0.0;
               for (k = 0; k <= i; k++) {
                  g += V[k][i+1] * V[k][j];
               }
               for (k = 0; k <= i; k++) {
                  V[k][j] -= g * d[k];
               }
            }
         }
         for (k = 0; k <= i; k++) {
            V[k][i+1] = 0.0;
         }
      }
      for (j = 0; j < n; j++) {
         d[j] = V[n-1][j];
         V[n-1][j] = 0.0;
      }
      V[n-1][n-1] = 1.0;
      e[0] = 0.0;

}  


#if 0
 
static void
WriteMaxErrorInfo(cmaes_t *t)
{
  int i,j, N=t->sp.N; 
  char *s = (char *)new_void(200+30*(N+2), sizeof(char)); s[0] = '\0';
  
  sprintf( s+strlen(s),"\nComplete Info\n");
  sprintf( s+strlen(s)," Gen       %20.12g\n", t->gen);
  sprintf( s+strlen(s)," Dimension %d\n", N);
  sprintf( s+strlen(s)," sigma     %e\n", t->sigma);
  sprintf( s+strlen(s)," lastminEW %e\n", 
           t->dLastMinEWgroesserNull);
  sprintf( s+strlen(s)," maxKond   %e\n\n", t->dMaxSignifKond);
  sprintf( s+strlen(s),"     x-vector          rgD     Basis...\n");
  ERRORMESSAGE( s,0,0,0);
  s[0] = '\0';
  for (i = 0; i < N; ++i)
    {
      sprintf( s+strlen(s), " %20.12e", t->rgxmean[i]);
      sprintf( s+strlen(s), " %10.4e", t->rgD[i]);
      for (j = 0; j < N; ++j)
        sprintf( s+strlen(s), " %10.2e", t->B[i][j]);
      ERRORMESSAGE( s,0,0,0);
      s[0] = '\0';
    }
  ERRORMESSAGE( "\n",0,0,0);
  free( s);
}  
#endif

 
 
 





 
 
void 
cmaes_timings_init(cmaes_timings_t *t) {
  t->totaltotaltime = 0; 
  cmaes_timings_start(t);
}
void 
cmaes_timings_start(cmaes_timings_t *t) {
  t->totaltime = 0;
  t->tictoctime = 0;
  t->lasttictoctime = 0;
  t->istic = 0;
  t->lastclock = clock();
  t->lasttime = time(NULL);
  t->lastdiff = 0;
  t->tictoczwischensumme = 0;
  t->isstarted = 1;
}

double 
cmaes_timings_update(cmaes_timings_t *t) {


 
  double diffc, difft;
  clock_t lc = t->lastclock;  
  time_t lt = t->lasttime;    

  if (t->isstarted != 1)
    FATAL("cmaes_timings_started() must be called before using timings... functions",0,0,0);

  t->lastclock = clock();  
  t->lasttime = time(NULL);

  diffc = (double)(t->lastclock - lc) / CLOCKS_PER_SEC;  
  difft = difftime(t->lasttime, lt);                     

  t->lastdiff = difft;  

   
  if (diffc > 0 && difft < 1000)
    t->lastdiff = diffc;

  if (t->lastdiff < 0)
    FATAL("BUG in time measurement", 0, 0, 0);

  t->totaltime += t->lastdiff;
  t->totaltotaltime += t->lastdiff;
  if (t->istic) {
    t->tictoczwischensumme += t->lastdiff;
    t->tictoctime += t->lastdiff;
  }

  return t->lastdiff; 
}

void
cmaes_timings_tic(cmaes_timings_t *t) {
  if (t->istic) {  
    ERRORMESSAGE("Warning: cmaes_timings_tic called twice without toc",0,0,0);
    return; 
  }
  cmaes_timings_update(t); 
  t->istic = 1; 
}

double
cmaes_timings_toc(cmaes_timings_t *t) {
  if (!t->istic) {
    ERRORMESSAGE("Warning: cmaes_timings_toc called without tic",0,0,0);
    return -1; 
  }
  cmaes_timings_update(t);
  t->lasttictoctime = t->tictoczwischensumme;
  t->tictoczwischensumme = 0;
  t->istic = 0;
  return t->lasttictoctime;
}

 
 
 
 
 
 
 
 



 
 
 

long 
cmaes_random_init( cmaes_random_t *t, long unsigned inseed)
{
  clock_t cloc = clock();

  t->flgstored = 0;
  t->rgrand = (long *) new_void(32, sizeof(long));
  if (inseed < 1) {
    while ((long) (cloc - clock()) == 0)
      ;  
    inseed = (long unsigned)labs((long)(100*time(NULL)+clock()));
  }
  return cmaes_random_Start(t, inseed);
}

void
cmaes_random_exit(cmaes_random_t *t)
{
  free( t->rgrand);
}

 
long cmaes_random_Start( cmaes_random_t *t, long unsigned inseed)
{
  long tmp;
  int i;

  t->flgstored = 0;
  t->startseed = inseed;  
  while (inseed > 2e9)   
    inseed /= 2;  
  if (inseed < 1)
    inseed = 1; 
  t->aktseed = inseed;
  for (i = 39; i >= 0; --i)
  {
    tmp = t->aktseed/127773;
    t->aktseed = 16807 * (t->aktseed - tmp * 127773)
      - 2836 * tmp;
    if (t->aktseed < 0) t->aktseed += 2147483647;
    if (i < 32)
      t->rgrand[i] = t->aktseed;
  }
  t->aktrand = t->rgrand[0];
  return inseed;
}

 
double cmaes_random_Gauss(cmaes_random_t *t)
{
  double x1, x2, rquad, fac;

  if (t->flgstored)
  {    
    t->flgstored = 0;
    return t->hold;
  }
  do 
  {
    x1 = 2.0 * cmaes_random_Uniform(t) - 1.0;
    x2 = 2.0 * cmaes_random_Uniform(t) - 1.0;
    rquad = x1*x1 + x2*x2;
  } while(rquad >= 1 || rquad <= 0);
  fac = sqrt(-2.0*log(rquad)/rquad);
  t->flgstored = 1;
  t->hold = fac * x1;
  return fac * x2;
}

 
double cmaes_random_Uniform( cmaes_random_t *t)
{
  long tmp;

  tmp = t->aktseed/127773;
  t->aktseed = 16807 * (t->aktseed - tmp * 127773)
    - 2836 * tmp;
  if (t->aktseed < 0) 
    t->aktseed += 2147483647;
  tmp = t->aktrand / 67108865;
  t->aktrand = t->rgrand[tmp];
  t->rgrand[tmp] = t->aktseed;
  return (double)(t->aktrand)/(2.147483647e9);
}

 
static void
printErrorStrings(FILE *fd, char const *s1, char const *s2,
                  char const *s3, char const *s4)
{
  if (s1 != NULL)
    fprintf(fd, "%s", s1);
  if (s2 != NULL)
    fprintf(fd, " %s", s2);
  if (s3 != NULL)
    fprintf(fd, " %s", s3);
  if (s4 != NULL)
    fprintf(fd, " %s", s4);
}

 
 
 
void
cmaes_readpara_init (cmaes_readpara_t *t,
               int dim, 
               const double * inxstart, 
               const double * inrgsigma,
               int inseed, 
               int lambda, 
               const char * filename)
{
  int i, N;
   
  t->filename = NULL;  
  t->rgsformat = (const char **) new_void(55, sizeof(char *));
  t->rgpadr = (void **) new_void(55, sizeof(void *)); 
  t->rgskeyar = (const char **) new_void(11, sizeof(char *));
  t->rgp2adr = (double ***) new_void(11, sizeof(double **));
  t->weigkey = (char *)new_void(7, sizeof(char)); 

   
  i = 0;
  t->rgsformat[i] = " N %d";        t->rgpadr[i++] = (void *) &t->N; 
  t->rgsformat[i] = " seed %d";    t->rgpadr[i++] = (void *) &t->seed;
  t->rgsformat[i] = " stopMaxFunEvals %lg"; t->rgpadr[i++] = (void *) &t->stopMaxFunEvals;
  t->rgsformat[i] = " stopMaxIter %lg"; t->rgpadr[i++] = (void *) &t->stopMaxIter;
  t->rgsformat[i] = " stopFitness %lg"; t->rgpadr[i++]=(void *) &t->stStopFitness.val;
  t->rgsformat[i] = " stopTolFun %lg"; t->rgpadr[i++]=(void *) &t->stopTolFun;
  t->rgsformat[i] = " stopTolFunHist %lg"; t->rgpadr[i++]=(void *) &t->stopTolFunHist;
  t->rgsformat[i] = " stopTolX %lg"; t->rgpadr[i++]=(void *) &t->stopTolX;
  t->rgsformat[i] = " stopTolUpXFactor %lg"; t->rgpadr[i++]=(void *) &t->stopTolUpXFactor;
  t->rgsformat[i] = " lambda %d";      t->rgpadr[i++] = (void *) &t->lambda;
  t->rgsformat[i] = " mu %d";          t->rgpadr[i++] = (void *) &t->mu;
  t->rgsformat[i] = " weights %5s";    t->rgpadr[i++] = (void *) t->weigkey;
  t->rgsformat[i] = " fac*cs %lg";t->rgpadr[i++] = (void *) &t->cs;
  t->rgsformat[i] = " fac*damps %lg";   t->rgpadr[i++] = (void *) &t->damps;
  t->rgsformat[i] = " ccumcov %lg";    t->rgpadr[i++] = (void *) &t->ccumcov;
  t->rgsformat[i] = " mucov %lg";     t->rgpadr[i++] = (void *) &t->mucov;
  t->rgsformat[i] = " fac*ccov %lg";  t->rgpadr[i++]=(void *) &t->ccov;
  t->rgsformat[i] = " diagonalCovarianceMatrix %lg"; t->rgpadr[i++]=(void *) &t->diagonalCov;
  t->rgsformat[i] = " updatecov %lg"; t->rgpadr[i++]=(void *) &t->updateCmode.modulo;
  t->rgsformat[i] = " maxTimeFractionForEigendecompostion %lg"; t->rgpadr[i++]=(void *) &t->updateCmode.maxtime;
  t->rgsformat[i] = " resume %59s";    t->rgpadr[i++] = (void *) t->resumefile;
  t->rgsformat[i] = " fac*maxFunEvals %lg";   t->rgpadr[i++] = (void *) &t->facmaxeval;
  t->rgsformat[i] = " fac*updatecov %lg"; t->rgpadr[i++]=(void *) &t->facupdateCmode;
  t->n1para = i; 
  t->n1outpara = i-2;  

   
  i = 0;
  t->rgskeyar[i]  = " typicalX %d";   t->rgp2adr[i++] = &t->typicalX;
  t->rgskeyar[i]  = " initialX %d";   t->rgp2adr[i++] = &t->xstart;
  t->rgskeyar[i]  = " initialStandardDeviations %d"; t->rgp2adr[i++] = &t->rgInitialStds;
  t->rgskeyar[i]  = " diffMinChange %d"; t->rgp2adr[i++] = &t->rgDiffMinChange;
  t->n2para = i;  

  t->N = dim;
  t->seed = (unsigned) inseed; 
  t->xstart = NULL; 
  t->typicalX = NULL;
  t->typicalXcase = 0;
  t->rgInitialStds = NULL; 
  t->rgDiffMinChange = NULL; 
  t->stopMaxFunEvals = -1;
  t->stopMaxIter = -1;
  t->facmaxeval = 1; 
  t->stStopFitness.flg = -1;
  t->stopTolFun = 1e-12; 
  t->stopTolFunHist = 1e-13; 
  t->stopTolX = 0;   
  t->stopTolUpXFactor = 1e3; 

  t->lambda = lambda;
  t->mu = -1;
  t->mucov = -1;
  t->weights = NULL;
  strcpy(t->weigkey, "log");

  t->cs = -1;
  t->ccumcov = -1;
  t->damps = -1;
  t->ccov = -1;

  t->diagonalCov = 0;  

  t->updateCmode.modulo = -1;  
  t->updateCmode.maxtime = -1;
  t->updateCmode.flgalways = 0;
  t->facupdateCmode = 1;
  strcpy(t->resumefile, "_no_");

   
  if (!isNoneStr(filename) && (!filename || strcmp(filename, "writeonly") != 0))
    cmaes_readpara_ReadFromFile(t, filename);

  if (t->N <= 0)
    t->N = dim;

  N = t->N; 
  if (N == 0)
    FATAL("cmaes_readpara_t(): problem dimension N undefined.\n",
          "  (no default value available).",0,0); 
  if (t->xstart == NULL && inxstart == NULL && t->typicalX == NULL) {
    ERRORMESSAGE("Error: initialX undefined. typicalX = 0.5...0.5 used.","","","");
    printf("\nError: initialX undefined. typicalX = 0.5...0.5 used.\n");
  }
  if (t->rgInitialStds == NULL && inrgsigma == NULL) {
     
    ERRORMESSAGE("Error: initialStandardDeviations undefined. 0.3...0.3 used.","","","");
    printf("\nError: initialStandardDeviations undefined. 0.3...0.3 used.\n");
  }

  if (t->xstart == NULL) {
    t->xstart = new_double(N);

     
    if (inxstart != NULL) { 
      for (i=0; i<N; ++i)
        t->xstart[i] = inxstart[i];
    }
     
    else {
      t->typicalXcase = 1;
      for (i=0; i<N; ++i)
        t->xstart[i] = (t->typicalX == NULL) ? 0.5 : t->typicalX[i]; 
    }
  }  
  
  if (t->rgInitialStds == NULL) {
    t->rgInitialStds = new_double(N);
    for (i=0; i<N; ++i)
      t->rgInitialStds[i] = (inrgsigma == NULL) ? 0.3 : inrgsigma[i];
  }

  t->flgsupplemented = 0;

}  

 
 
void cmaes_readpara_exit(cmaes_readpara_t *t)
{
  if (t->filename != NULL)
    free( t->filename);
  if (t->xstart != NULL)  
    free( t->xstart);
  if (t->typicalX != NULL)
    free( t->typicalX);
  if (t->rgInitialStds != NULL)
    free( t->rgInitialStds);
  if (t->rgDiffMinChange != NULL)
    free( t->rgDiffMinChange);
  if (t->weights != NULL)
    free( t->weights);

  free((void*)t->rgsformat);
  free(t->rgpadr);
  free((void*)t->rgskeyar);
  free(t->rgp2adr);
  free(t->weigkey);
}

 
 
void 
cmaes_readpara_ReadFromFile(cmaes_readpara_t *t, const char * filename)
{
  char s[1000];
  const char *ss = "cmaes_initials.par";
  int ipara, i;
  int size;
  FILE *fp;
  if (filename == NULL) {
    filename = ss;
  }
  t->filename = NULL;  
  fp = fopen( filename, "r"); 
  if (fp == NULL) {
    ERRORMESSAGE("cmaes_ReadFromFile(): could not open '", filename, "'",0);
    return;
  }
  for (ipara=0; ipara < t->n1para; ++ipara)
    {
      rewind(fp);
      while(fgets(s, sizeof(s), fp) != NULL)
        {  
          if (s[0] == '#' || s[0] == '%')
            continue;
          if(sscanf(s, t->rgsformat[ipara], t->rgpadr[ipara]) == 1) {
            if (strncmp(t->rgsformat[ipara], " stopFitness ", 13) == 0)
              t->stStopFitness.flg = 1;
            break;
          }
        }
    }  
  if (t->N <= 0)
    FATAL("cmaes_readpara_ReadFromFile(): No valid dimension N",0,0,0); 
  for (ipara=0; ipara < t->n2para; ++ipara)
    {
      rewind(fp);
      while(fgets(s, sizeof(s), fp) != NULL)  
        {  
          if (s[0] == '#' || s[0] == '%')
            continue;
          if(sscanf(s, t->rgskeyar[ipara], &size) == 1) {  
            if (size > 0) {
              *t->rgp2adr[ipara] = new_double(t->N);
              for (i=0;i<size&&i<t->N;++i)  
                if (fscanf(fp, " %lf", &(*t->rgp2adr[ipara])[i]) != 1)
                  break;
              if (i<size && i < t->N) {
                ERRORMESSAGE("cmaes_readpara_ReadFromFile ", filename, ": ",0); 
                FATAL( "'", t->rgskeyar[ipara], 
                       "' not enough values found.\n", 
                       "   Remove all comments between numbers.");
              }
              for (; i < t->N; ++i)  
                (*t->rgp2adr[ipara])[i] = (*t->rgp2adr[ipara])[i%size];
            }
          }
        }  
    }  
  fclose(fp);
  assign_string(&(t->filename), filename);  
  return;
}  

 
 
void
cmaes_readpara_WriteToFile(cmaes_readpara_t *t, const char *filenamedest)
{
  int ipara, i; 
  size_t len;
  time_t ti = time(NULL);
  FILE *fp = fopen( filenamedest, "a"); 
  if(fp == NULL) {
    ERRORMESSAGE("cmaes_WriteToFile(): could not open '", 
                 filenamedest, "'",0);
    return;
  }
  fprintf(fp, "\n# Read from %s at %s\n", t->filename ? t->filename : "", 
          asctime(localtime(&ti)));  
  for (ipara=0; ipara < 1; ++ipara) {
    fprintf(fp, t->rgsformat[ipara], *(int *)t->rgpadr[ipara]);
    fprintf(fp, "\n");
  }
  for (ipara=0; ipara < t->n2para; ++ipara) {
    if(*t->rgp2adr[ipara] == NULL)
      continue;
    fprintf(fp, t->rgskeyar[ipara], t->N);
    fprintf(fp, "\n");
    for (i=0; i<t->N; ++i)
      fprintf(fp, "%7.3g%c", (*t->rgp2adr[ipara])[i], (i%5==4)?'\n':' ');
    fprintf(fp, "\n");
  }
  for (ipara=1; ipara < t->n1outpara; ++ipara) {
    if (strncmp(t->rgsformat[ipara], " stopFitness ", 13) == 0)
      if(t->stStopFitness.flg == 0) {
        fprintf(fp, " stopFitness\n");
        continue;
      }
    len = strlen(t->rgsformat[ipara]);
    if (t->rgsformat[ipara][len-1] == 'd')  
      fprintf(fp, t->rgsformat[ipara], *(int *)t->rgpadr[ipara]);
    else if (t->rgsformat[ipara][len-1] == 's')  
      fprintf(fp, t->rgsformat[ipara], (char *)t->rgpadr[ipara]);
    else { 
      if (strncmp(" fac*", t->rgsformat[ipara], 5) == 0) {
        fprintf(fp, " ");
        fprintf(fp, t->rgsformat[ipara]+5, *(double *)t->rgpadr[ipara]);
      } else
        fprintf(fp, t->rgsformat[ipara], *(double *)t->rgpadr[ipara]);
    }
    fprintf(fp, "\n");
  }  
  fprintf(fp, "\n");
  fclose(fp); 
}  

 
 
void 
cmaes_readpara_SupplementDefaults(cmaes_readpara_t *t)





 
{
  double t1, t2;
  int N = t->N; 
  clock_t cloc = clock();
  
  if (t->flgsupplemented)
    FATAL("cmaes_readpara_SupplementDefaults() cannot be called twice.",0,0,0);
  if (t->seed < 1) {
    while ((int) (cloc - clock()) == 0)
      ;  
    t->seed = (unsigned int)labs((long)(100*time(NULL)+clock()));
  }

  if (t->stStopFitness.flg == -1)
    t->stStopFitness.flg = 0;

  if (t->lambda < 2)
    t->lambda = 4+(int)(3*log((double)N));
  if (t->mu == -1) {
    t->mu = t->lambda/2; 
    cmaes_readpara_SetWeights(t, t->weigkey);
  }
  if (t->weights == NULL)
    cmaes_readpara_SetWeights(t, t->weigkey);

  if (t->cs > 0)  
    t->cs *= (t->mueff + 2.) / (N + t->mueff + 3.);
  if (t->cs <= 0 || t->cs >= 1)
    t->cs = (t->mueff + 2.) / (N + t->mueff + 3.);

  if (t->ccumcov <= 0 || t->ccumcov > 1)
    t->ccumcov = 4. / (N + 4);
  
  if (t->mucov < 1) {
    t->mucov = t->mueff;
  }
  t1 = 2. / ((N+1.4142)*(N+1.4142));
  t2 = (2.*t->mueff-1.) / ((N+2.)*(N+2.)+t->mueff);
  t2 = (t2 > 1) ? 1 : t2;
  t2 = (1./t->mucov) * t1 + (1.-1./t->mucov) * t2;
  if (t->ccov >= 0)  
    t->ccov *= t2;
  if (t->ccov < 0 || t->ccov > 1)  
    t->ccov = t2;

  if (t->diagonalCov == -1)
    t->diagonalCov = 2 + 100. * N / sqrt((double)t->lambda); 

  if (t->stopMaxFunEvals == -1)   
    t->stopMaxFunEvals = t->facmaxeval*900*(N+3)*(N+3); 
  else
    t->stopMaxFunEvals *= t->facmaxeval;

  if (t->stopMaxIter == -1)
    t->stopMaxIter = ceil((double)(t->stopMaxFunEvals / t->lambda)); 

  if (t->damps < 0) 
    t->damps = 1;  
  t->damps = t->damps 
    * (1 + 2*douMax(0., sqrt((t->mueff-1.)/(N+1.)) - 1))      
    * douMax(0.3, 1. -                                        
                  (double)N / (1e-6+douMin(t->stopMaxIter, t->stopMaxFunEvals/t->lambda))) 
    + t->cs;                                                  

  if (t->updateCmode.modulo < 0)
    t->updateCmode.modulo = 1./t->ccov/(double)(N)/10.;
  t->updateCmode.modulo *= t->facupdateCmode;
  if (t->updateCmode.maxtime < 0)
    t->updateCmode.maxtime = 0.20;  
    
  t->flgsupplemented = 1;

}  

   
 
 
void 
cmaes_readpara_SetWeights(cmaes_readpara_t *t, const char * mode)
{
  double s1, s2;
  int i;

  if(t->weights != NULL)
    free( t->weights); 
  t->weights = new_double(t->mu);
  if (strcmp(mode, "lin") == 0)
    for (i=0; i<t->mu; ++i) 
      t->weights[i] = t->mu - i;
  else if (strncmp(mode, "equal", 3) == 0)
    for (i=0; i<t->mu; ++i) 
      t->weights[i] = 1;
  else if (strcmp(mode, "log") == 0) 
    for (i=0; i<t->mu; ++i) 
      t->weights[i] = log(t->mu+1.)-log(i+1.); 
  else
    for (i=0; i<t->mu; ++i) 
      t->weights[i] = log(t->mu+1.)-log(i+1.); 

   
  for (i=0, s1=0, s2=0; i<t->mu; ++i) {
    s1 += t->weights[i];
    s2 += t->weights[i]*t->weights[i];
  }
  t->mueff = s1*s1/s2;
  for (i=0; i<t->mu; ++i) 
    t->weights[i] /= s1;

  if(t->mu < 1 || t->mu > t->lambda || 
     (t->mu==t->lambda && t->weights[0]==t->weights[t->mu-1]))
    FATAL("cmaes_readpara_SetWeights(): invalid setting of mu or lambda",0,0,0);

}  

 
 
static int 
isNoneStr(const char * filename)
{
  if (filename && (strcmp(filename, "no") == 0 
        || strcmp(filename, "non") == 0
        || strcmp(filename, "none") == 0))
    return 1;
  
  return 0;
}
        
 
 
static double 
douSquare(double d)
{
  return d*d;
}
static double
douMax( double i, double j)
{
  return i > j ? i : j;
}
static double
douMin( double i, double j)
{
  return i < j ? i : j;
}
static double
rgdouMax( const double *rgd, int len)
{
  int i;
  double max = rgd[0];
  for (i = 1; i < len; ++i)
    max = (max < rgd[i]) ? rgd[i] : max;
  return max;
}

static double
rgdouMin( const double *rgd, int len)
{
  int i;
  double min = rgd[0];
  for (i = 1; i < len; ++i)
    min = (min > rgd[i]) ? rgd[i] : min;
  return min;
}

static int    
MaxIdx( const double *rgd, int len)
{
  int i, res;
  for(i=1, res=0; i<len; ++i)
    if(rgd[i] > rgd[res])
      res = i;
  return res;
}
static int    
MinIdx( const double *rgd, int len)
{
  int i, res;
  for(i=1, res=0; i<len; ++i)
    if(rgd[i] < rgd[res])
      res = i;
  return res;
}

static double 
myhypot(double a, double b) 
 
{
  double r = 0;
  if (fabs(a) > fabs(b)) {
    r = b/a;
    r = fabs(a)*sqrt(1+r*r);
  } else if (b != 0) {
    r = a/b;
    r = fabs(b)*sqrt(1+r*r);
  }
  return r;
}

static int SignOfDiff(const void *d1, const void * d2) 
{ 
  return *((double *) d1) > *((double *) d2) ? 1 : -1; 
} 

#if 1
 
static void Sorted_index(const double *rgFunVal, int *iindex, int n)
{
  int i, j;
  for (i=1, iindex[0]=0; i<n; ++i) {
    for (j=i; j>0; --j) {
      if (rgFunVal[iindex[j-1]] < rgFunVal[i])
        break;
      iindex[j] = iindex[j-1];  
    }
    iindex[j] = i;  
  }
}
#endif 

static void * new_void(int n, size_t size)
{
  static char s[70];
  void *p = calloc((unsigned) n, size);
  if (p == NULL) {
    sprintf(s, "new_void(): calloc(%ld,%ld) failed",(long)n,(long)size);
    FATAL(s,0,0,0);
  }
  return p;
}

double * 
cmaes_NewDouble(int n) 
{
  return new_double(n);
}

static double * new_double(int n)
{
  static char s[170];
  double *p = (double *) calloc((unsigned) n, sizeof(double));
  if (p == NULL) {
    sprintf(s, "new_double(): calloc(%ld,%ld) failed",
            (long)n,(long)sizeof(double));
    FATAL(s,0,0,0);
  }
  return p;
}

static char * new_string(const char *ins)
{
  static char s[170];
  unsigned i; 
  char *p; 
  unsigned len = (unsigned) ((ins != NULL) ? strlen(ins) : 0);
  
  if (len > 1000) {
    FATAL("new_string(): input string length was larger then 1000 ",
        "(possibly due to uninitialized char *filename)",0,0);
  }
  
  p = (char *) calloc( len + 1, sizeof(char));
  if (p == NULL) {
    sprintf(s, "new_string(): calloc(%ld,%ld) failed",
            (long)len,(long)sizeof(char));
    FATAL(s,0,0,0);
  }
  for (i = 0; i < len; ++i)
    p[i] = ins[i];
  return p;
}
static void assign_string(char ** pdests, const char *ins)
{
    if (*pdests)
        free(*pdests);
    if (ins == NULL)
      *pdests = NULL;
    else    
      *pdests = new_string(ins);
}

 
 

 
void 
cmaes_FATAL(char const *s1, char const *s2, char const *s3, 
            char const *s4)
{
  time_t t = time(NULL);
  ERRORMESSAGE( s1, s2, s3, s4);
  ERRORMESSAGE("*** Exiting cmaes_t ***",0,0,0);
  printf("\n -- %s ", asctime(localtime(&t)));
  printErrorStrings(stdout, s1, s2, s3, s4);
  printf("\n");
  printf(" *** CMA-ES ABORTED, see errcmaes.err *** \n");
  fflush(stdout);
  exit(1);
}

 
static void 
FATAL(char const *s1, char const *s2, char const *s3, 
      char const *s4)
{
  cmaes_FATAL(s1, s2, s3, s4);
}

 
static void ERRORMESSAGE( char const *s1, char const *s2, 
                          char const *s3, char const *s4)
{
#if 1
  

 
  FILE *fp = fopen( "errcmaes.err", "a");
  if (fp == NULL) {
    printf("\nFATAL ERROR: ");
    printErrorStrings(stdout, s1, s2, s3, s4);
    printf("\n");
    printf("cmaes_t could not open file 'errcmaes.err'.");
    printf("\n *** CMA-ES ABORTED *** ");
    fflush(stdout);
    exit(1);
  } else {
    time_t t = time(NULL);
    fprintf(fp, "\n -- %s ", asctime(localtime(&t)));
    printErrorStrings(fp, s1, s2, s3, s4);
    fprintf(fp, "\n");
    fclose (fp);
  }
#endif
}

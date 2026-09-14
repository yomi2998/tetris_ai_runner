 
 
 
 
 








 
#ifndef NH_cmaes_h   
#define NH_cmaes_h 

#include <time.h>

typedef struct 


 
{
   
  long int startseed;
  long int aktseed;
  long int aktrand;
  long int *rgrand;
  
   
  short flgstored;
  double hold;
} cmaes_random_t;

typedef struct 


 
{
   
  double totaltime;  
  double totaltotaltime;
  double tictoctime; 
  double lasttictoctime;
  
   
  clock_t lastclock;
  time_t lasttime;
  clock_t ticclock;
  time_t tictime;
  short istic;
  short isstarted; 

  double lastdiff;
  double tictoczwischensumme;
} cmaes_timings_t;

typedef struct 



 
{
  char * filename;   
  short flgsupplemented; 
  
   
  int N;  
  unsigned int seed; 
  double * xstart; 
  double * typicalX; 
  int typicalXcase;
  double * rgInitialStds;
  double * rgDiffMinChange; 

   
  double stopMaxFunEvals; 
  double facmaxeval;
  double stopMaxIter; 
  struct { int flg; double val; } stStopFitness; 
  double stopTolFun;
  double stopTolFunHist;
  double stopTolX;
  double stopTolUpXFactor;

   
  int lambda;           
  int mu;               
  double mucov, mueff;  
  double *weights;      
  double damps;         
  double cs;            
  double ccumcov;       
  double ccov;          
  double diagonalCov;   
  struct { int flgalways; double modulo; double maxtime; } updateCmode;
  double facupdateCmode;

   

  char *weigkey; 
  char resumefile[99];
  const char **rgsformat;
  void **rgpadr;
  const char **rgskeyar;
  double ***rgp2adr;
  int n1para, n1outpara;
  int n2para;
} cmaes_readpara_t;

typedef struct 


 
{
  const char *version;
   
  cmaes_readpara_t sp;
  cmaes_random_t rand;  

  double sigma;   

  double *rgxmean;   
  double *rgxbestever; 
  double **rgrgx;    
  int *index;        
  double *arFuncValueHist;

  short flgIniphase;  
  short flgStop; 

  double chiN; 
  double **C;   
  double **B;   
  double *rgD;  

  double *rgpc;
  double *rgps;
  double *rgxold; 
  double *rgout; 
  double *rgBDz;    
  double *rgdTmp;   
  double *rgFuncValue; 
  double *publicFitness;  

  double gen;  
  double countevals;
  double state;  

  double maxdiagC;  
  double mindiagC;
  double maxEW;
  double minEW;

  char sOutString[330];  

  short flgEigensysIsUptodate;
  short flgCheckEigen;  
  double genOfEigensysUpdate; 
  cmaes_timings_t eigenTimings;
 
  double dMaxSignifKond; 				     
  double dLastMinEWgroesserNull;

  short flgresumedone; 

  time_t printtime; 
  time_t writetime;  
  time_t firstwritetime;
  time_t firstprinttime; 

} cmaes_t; 


#endif 

 
 
 
 
 









 
#include "cmaes.h"

 
 
 

#ifdef __cplusplus
extern "C" {
#endif

 
double * cmaes_init(cmaes_t *, int dimension , double *xstart, 
		double *stddev, long seed, int lambda, 
		const char *input_parameter_filename);
void cmaes_init_para(cmaes_t *, int dimension , double *xstart, 
		double *stddev, long seed, int lambda, 
		const char *input_parameter_filename);
double * cmaes_init_final(cmaes_t *);
void cmaes_resume_distribution(cmaes_t *evo_ptr, char *filename);
void cmaes_exit(cmaes_t *);

 
double * const * cmaes_SamplePopulation(cmaes_t *);
double *         cmaes_UpdateDistribution(cmaes_t *, 
					  const double *rgFitnessValues);
const char *     cmaes_TestForTermination(cmaes_t *);

 
double * const * cmaes_ReSampleSingle( cmaes_t *t, int index);
double const *   cmaes_ReSampleSingle_old(cmaes_t *, double *rgx); 
double *         cmaes_SampleSingleInto( cmaes_t *t, double *rgx);
void             cmaes_UpdateEigensystem(cmaes_t *, int flgforce);

 
double         cmaes_Get(cmaes_t *, char const *keyword);
const double * cmaes_GetPtr(cmaes_t *, char const *keyword);  
double *       cmaes_GetNew( cmaes_t *t, char const *keyword);  
double *       cmaes_GetInto( cmaes_t *t, char const *keyword, double *mem);  

 
void           cmaes_ReadSignals(cmaes_t *, char const *filename);
void           cmaes_WriteToFile(cmaes_t *, const char *szKeyWord,
                                 const char *output_filename); 
char *         cmaes_SayHello(cmaes_t *);
 
double *       cmaes_NewDouble(int n);  
void           cmaes_FATAL(char const *s1, char const *s2, char const *s3, 
			   char const *s4);

#ifdef __cplusplus
} 
#endif


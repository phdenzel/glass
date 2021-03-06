#include <Python.h>
#include <numpy/arrayobject.h>

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <time.h>
#include <unistd.h>
#include <sys/times.h>
#include <signal.h>

#include <sched.h>
#include <errno.h>

#include "timing.h"

/*==========================================================================*/
/* Use double or long double as the data type for the simplex tableau.      */
/* double is much faster but long double has more precision.                */
/* If a given problem is small enough that double will suffice then there   */
/* is no benefit from using long double.                                    */
/*==========================================================================*/
#define USE_LONG_DOUBLE 0

#define WITH_GOOGLE_PROFILER 0

#if WITH_GOOGLE_PROFILER
#include <google/profiler.h>
#endif

/*==========================================================================*/
/* Timing variables                                                         */
/*==========================================================================*/
CPUDEFS

/*==========================================================================*/
/* Debugging tools                                                          */
/*==========================================================================*/
#define DBG_LEVEL 0
#define DBG(lvl) if (DBG_LEVEL >= lvl) 


#if USE_LONG_DOUBLE
typedef long double dble_t;
#define EPS ((dble_t)1.0e-17L)
#define INF ((dble_t)1.0e12L)
#define SML ((dble_t)1.0e-8L)
#define EPS_EXP (-56)
#define ABS fabsl
#else
//typedef double dble_t __attribute__ ((aligned(8)));
#define dble_t double
#define EPS ((dble_t)1e-14)
#define INF ((dble_t)1e+12) 
#define SML ((dble_t)1e-08)
#define EPS_EXP (-46)
#define ABS fabs
#endif

#if 0
#define ABS(n) \
    (sizeof(n) == sizeof(float) \
     ? fabsf(n) \
     : sizeof(n) == sizeof(double) \
     ? fabs(n) : fabsl(n))
#endif

#define LOG(n) \
    (sizeof(n) == sizeof(float) \
     ? logf(n) \
     : sizeof(n) == sizeof(double) \
     ? log(n) : logl(n))

#define FREXP(n) \
    (sizeof(n) == sizeof(float) \
     ? frexpf(n) \
     : sizeof(n) == sizeof(double) \
     ? frexp(n) : frexpl(n))

#define FMA(x,y,z) \
    (sizeof(x) == sizeof(float) \
     ? fmaf(x,y,z) \
     : sizeof(x) == sizeof(double) \
     ? fma(x,y,z) : fmal(x,y,z))

#define TEST(x) \
    (sizeof(x) == sizeof(float) \
     ? fprintf(stderr, "float\n") \
     : sizeof(x) == sizeof(double) \
     ? fprintf(stderr, "double\n") : fprintf(stderr, "long double\n"))

enum 
{
    INFEASIBLE     = 0,
    FEASIBLE       = 1,
    NOPIVOT        = 2,
    FOUND_PIVOT    = 3,
    UNBOUNDED      = 4,
};

inline int32_t min(int32_t a, int32_t b)
{
    return (a < b) ? a : b;
}

inline dble_t rtz(dble_t v)
{
    //fprintf(stderr, "WARNING: rtz turned off!\n");
    //return v;
    //if (ABS(v) <= EPS) return 0;
    return v;
    //return v * (ABS(v) > EPS);
}

/*==========================================================================*/
/* Memory allocation wrappers.                                              */
/*==========================================================================*/

#if DBG_LEVEL >= 10000
long total_alloc = 0;
#define CALLOC(type, num) \
    (total_alloc += sizeof(type) * (num), \
    fprintf(stderr, "c'allocating %ld bytes [already alloc'd: %ld].\n", sizeof(type) * (num), total_alloc), \
    ((type *)calloc((num), sizeof(type))))
#else
#define CALLOC(type, num) ((type *)calloc((num), sizeof(type)))
#endif

#define MALLOC(type, num) ((type *)malloc((num) * sizeof(type)))

/*==========================================================================*/
/* Data structures for the table and arrays.                                */
/*==========================================================================*/
typedef struct
{
    uint32_t cols;
    uint32_t rows;
    dble_t * restrict orig;
    dble_t * restrict data;
    dble_t * restrict pcol;
} matrix_t __attribute__ ((aligned(8)));

typedef struct
{
    int32_t step;
    int32_t nthreads;
    double obj_val;
    int32_t Z;
} report_t;
report_t report = {0,0,0.0};

void doPivot0(
    matrix_t * restrict tabl,
    const long L, const long R,
    const dble_t piv, 
    const int32_t lpiv, const int32_t rpiv,
    dble_t PREC);



/*==========================================================================*/
/* The worker threads can be asked to do different things.                  */
/* They can either do the normal pivot operation, or they can capture       */
/* a portion of the table memory by allocating memory local to their CPU.   */
/*==========================================================================*/
#define PT_ACTION_STEAL_TABLE_COLS  (1)
#define PT_ACTION_DO_PIVOT          (2)

/* Do we need to update the columns that each thread is working on? */
static int32_t need_assign_pivot_threads;

PyObject *samplex_pivot(PyObject *self, PyObject *args);
PyObject *set_rnd_cseed(PyObject *self, PyObject *args);

static PyMethodDef csamplex_methods[] = 
{
    {"pivot", samplex_pivot, METH_O, "pivot"},
    {"set_rnd_cseed", set_rnd_cseed, METH_O, "set_rnd_cseed"},
    {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC initcsamplex()
{
    (void)Py_InitModule("csamplex", csamplex_methods);
}

//@ Now the heart of the algorithm.  In each step of the
//iteration we choose a pivot element (|lpiv,rpiv|, whose actual indices
//are |rpivq|), and then pivot the table.

//To choose a pivot, we start by scanning the top row.  For any top-row
//entry $>$ that for the `best' candidate pivot we have (or $>0$), we
//find the maximum allowed increase, and accept the new pivot if it's
//better.  In case of degeneracies, we prefer to make artificial
//variables leave, otherwise make higher-index variables leave.

//If no pivot is found, we set |conv=true|.  But if a column we try
//gives no pivot, the function is unbounded.

/*==========================================================================*/
/* choosePivot                                                              */
/*==========================================================================*/

#if 0
int32_t choose_pivot0(matrix_t *tabl, int32_t *left, int32_t *right, long L, long R,
                      int32_t *lpiv0, int32_t *rpiv0, dble_t *piv0)
{ 
    typedef struct
    {
        int32_t l;
        dble_t cpiv, cinc, col0;
    } thread_t;

    int32_t k, r;
    dble_t * restrict bcol = &tabl->data[0];

    thread_t * restrict t = malloc((R+1) * sizeof(thread_t));
    memset(t, 0, (R+1) * sizeof(thread_t));

    int32_t accept;
    int32_t l, cleft;
    dble_t cpiv, cinc, tinc;
    dble_t * restrict col;

    DBG(3) fprintf(stderr, "> choosePivot()\n");

#if 0
    #pragma omp parallel for  \
            private(col, l, cleft, cpiv, cinc, k, tinc, accept) \
            shared(bcol, t, L, tabl)
#endif
    for (r = 1; r <= R; r++)
    {
        col = &tabl->data[r * tabl->rows + 0];

        DBG(2) fprintf(stderr, "r=%i\n", r);

        if (col[0] <= 0) continue;

        //----------------------------------------------------------------------
        // We now look for the column entry that causes the objective function
        // to increase the most.  Set |l,cleft,cpiv,cinc| for candidate pivot
        //----------------------------------------------------------------------

        l=0;
        cleft = 0;
        cinc = 0;

        for (k=1; k <= L; k++) 
        { 
            if (col[k] >= 0) continue; /* only interested in negative values */

            tinc = fabs(bcol[k]/col[k]);
            //assert(tinc >= 0);
            DBG(1) assert(!isinf(tinc));

            //------------------------------------------------------------------
            // Accept this pivot element if we haven't found anything yet.
            //------------------------------------------------------------------

            if (l == 0)
            {
                accept = 1;
                l = 1;
            }
            else if (ABS(tinc-cinc) >= EPS) 
            {
                accept = (tinc < cinc) + 8;
            }
            else 
            { 
                if (left[k] > 0 && cleft > 0)  
                    accept = (left[k] > cleft) + 2;
                else 
                    accept = (left[k] < cleft) + 4;
            }

            if ((accept&1)==0) continue; // && tinc > 0)


            //------------------------------------------------------------------
            // Remeber this pivot element for later.
            //------------------------------------------------------------------
            cleft = left[k]; 
            cinc  = tinc;

            t[r].l    = k;
            t[r].cinc = col[0] * tinc;
            t[r].cpiv = col[k];
            t[r].col0 = col[0];

            DBG(2) fprintf(stderr, "ACCEPTING tinc=%.15e %i %f\n", tinc, accept, bcol[k]);
        }
    }

    dble_t piv  = 0,
           coef = 0,
           inc  = 0;

    int32_t rpivq = 0,
            lpiv  = 0,
            rpiv  = 0;

    int32_t res = NOPIVOT; 

    for (r = 1; r <= R; r++)
    {
        if (t[r].col0 <= coef) continue;

        res = FOUND_PIVOT;

        //----------------------------------------------------------------------
        // Maybe update |lpiv,rpiv,rpivq,piv,inc,coef|
        //----------------------------------------------------------------------
        if (t[r].l == 0) 
        {
            lpiv = -1;
            rpiv = -1;
            piv  = -1;
            res = UNBOUNDED;
            break;
        }

        accept = 0;
        if (lpiv==0)
            accept = 1;
        else if (ABS(t[r].cinc-inc) < EPS)
            accept = (right[r] < rpivq);
        else  
            accept = (t[r].cinc > inc);

        if (accept)
        {
            lpiv  = t[r].l;
            rpiv  = r; 

            rpivq = right[r];

            piv = t[r].cpiv; 
            inc = t[r].cinc; 
            
            coef = t[r].col0;
        }
    }

    DBG(2) fprintf(stderr, "< choosePivot() %i %i %e %e\n", lpiv, rpiv, piv, inc);

    free(t);

    *lpiv0 = lpiv;
    *rpiv0 = rpiv;
    *piv0  =  piv;
    return res;
}
#else
#if 0
int32_t choose_pivot0(matrix_t *tabl, int32_t *left, int32_t *right, long L, long R,
                      int32_t *lpiv0, int32_t *rpiv0, dble_t *piv0)
{ 
    typedef struct
    {
        int32_t l;
        dble_t cpiv, cinc, col0;
    } thread_t;

    int32_t k, r;
    dble_t * restrict bcol = &tabl->data[0];

    thread_t * restrict t = malloc((R+1) * sizeof(thread_t));
    memset(t, 0, (R+1) * sizeof(thread_t));

    int32_t accept;
    int32_t l, cleft;
    dble_t cpiv, cinc, tinc;
    dble_t * restrict col;

    DBG(3) fprintf(stderr, "> choosePivot()\n");

//  #pragma omp parallel for  \
//          private(col, l, cleft, cpiv, cinc, k, tinc, accept) \
//          shared(bcol, t, L, tabl)
    for (r = 1; r <= R; r++)
    {
        col = &tabl->data[r * tabl->rows + 0];

        DBG(2) fprintf(stderr, "r=%i\n", r);

        //----------------------------------------------------------------------
        // We now look for the column entry that causes the objective function
        // to increase the most.  Set |l,cleft,cpiv,cinc| for candidate pivot
        //----------------------------------------------------------------------

        l = 0;
        cleft = 0;
        cpiv = 0;
        cinc = 0;

        t[r].col0 = col[0];

        int first_time = 1;

        for (k=1; k <= L; k++) 
        { 
            if (col[k] >= 0) continue; /* only interested in negative values */

            tinc = fabs(bcol[k]/col[k]);
            DBG(1) assert(!isinf(tinc));

            //if (tinc == 0) continue;

            //------------------------------------------------------------------
            // Accept this pivot element if we haven't found anything yet.
            //------------------------------------------------------------------
            accept = 0;
            if (first_time) 
            {
                accept = 1;
                first_time = 0;
            }
#if 0
            else if (ABS(tinc-cinc) < EPS) 
            { 
                if (left[k] > 0 && cleft > 0)  
                    accept = (left[k] > cleft) + 2;
                else 
                    accept = (left[k] < cleft) + 4;
            }
#endif
            else 
            {
                accept = (tinc < cinc) + 8;
            }

            if ((accept&1)==0) continue; // && tinc > 0)

            DBG(2) fprintf(stderr, "ACCEPTING tinc=%.15e %i %.20g\n", tinc, accept, col[k]);

            //------------------------------------------------------------------
            // Remember this pivot element for later.
            //------------------------------------------------------------------
            l     = k; 
            cleft = left[k]; 
            cpiv  = col[k]; 
            cinc  = tinc;

            t[r].l    = k;
            t[r].cinc = cinc;
            t[r].cpiv = cpiv;
        }
    }

    dble_t piv  = 0,
           coef = 0,
           inc  = 0;

    int32_t rpivq = 0,
            lpiv  = 0,
            rpiv  = 0;

    int32_t res = NOPIVOT; 

    for (r = 1; r <= R; r++)
    {
        DBG(2) fprintf(stderr, "col[r][0] is %g, coef is %g\n", t[r].col0, coef);
        if (t[r].col0 <= coef) continue;

        res = FOUND_PIVOT;

        //----------------------------------------------------------------------
        // Maybe update |lpiv,rpiv,rpivq,piv,inc,coef|
        //----------------------------------------------------------------------
        if (t[r].l == 0) 
        {
            lpiv = -1;
            rpiv = -1;
            piv  = -1;
            res = UNBOUNDED;
            break;
        }

        DBG(2) fprintf(stderr, "Comparing %g and %g\n", t[r].cinc, inc);

        accept = 0;
//      if (lpiv==0)
//          accept = 1;
//      else if (ABS(t[r].cinc-inc) < EPS)
//          accept = (right[r] < rpivq);
//      else  
            accept = (t[r].cinc > inc);

        if (accept)
        {
            lpiv  = t[r].l;
            rpiv  = r; 

            rpivq = right[r];

            piv = t[r].cpiv; 
            inc = t[r].cinc; 
            
            coef = t[r].col0;

            DBG(2) fprintf(stderr, "ACCEPTING inc=%.15e\n", inc);
        }
    }


    DBG(2) fprintf(stderr, "< choosePivot() %i %i %e %e\n", lpiv, rpiv, piv, inc);
    //fprintf(stderr, "< choosePivot() %i %i %e %e\n", lpiv, rpiv, piv, inc);

    assert(inc != 0);

    free(t);

    *lpiv0 = lpiv;
    *rpiv0 = rpiv;
    *piv0  =  piv;
    return res;
}
#else
int32_t choose_pivot0(matrix_t *tabl, int32_t *left, int32_t *right, long L, long R,
                      int32_t *lpiv0, int32_t *rpiv0, dble_t *piv0, int phase)
{ 
    typedef struct
    {
        int32_t l,r;
        dble_t piv, inc, col0, objf_inc;
    } state_t;

    state_t t = {0, 0, 0., 1e20, 0., -1.};

    int first_time;
    int32_t k, r;
    dble_t * restrict bcol = &tabl->data[0];

    int32_t accept;
    dble_t tinc;
    dble_t * restrict col;

    //fprintf(stderr, "> choosePivot()\n");

    //--------------------------------------------------------------------------
    // Look for a degeneracy. If we find such a row only look across that row
    // for a variable to swap out.
    //--------------------------------------------------------------------------
    int32_t start_k=1, end_k=L;
    int degeneracy = 0;
//  for (k=1;  k <= L;  k++) 
//  { 
//      if (bcol[k] == 0) 
//      {
//          bcol[k] = (1-drand48()) * 1e-8;
//          //start_k = k;
//          //end_k = k;
//          //degeneracy = 1;
//          //break;
//      }
//  }

    int32_t res = NOPIVOT; 


    //--------------------------------------------------------------------------
    // Examine each column. Those with a positive values in the first row are
    // potential pivot columns.
    //--------------------------------------------------------------------------
    for (r = 1; r <= R; r++)
    {
        col = &tabl->data[r * tabl->rows + 0];

        if (col[0] <= 1e-14) continue;

        DBG(2) fprintf(stderr, "r=%i\n", r);

        //----------------------------------------------------------------------
        // Assume that this column will show the system is unbounded.
        //----------------------------------------------------------------------
        res = UNBOUNDED;

        state_t s = {0, 0, 0., 0., 0., 0.};


        //----------------------------------------------------------------------
        // Find the column entry that is most restrictive. Save the entry that
        // increases the objective function the most. Entries must be negative.
        //----------------------------------------------------------------------
        first_time = 1;
        for (k=start_k;  k <= end_k;  k++) 
        { 
            //fprintf(stderr, "col[%i]=%.15e\n", k, col[k]);
            if (col[k] < 0)
            {
                accept = 0;

                //--------------------------------------------------------------
                //--------------------------------------------------------------
                res = FOUND_PIVOT;

                tinc = bcol[k] / fabs(col[k]);

//              if (bcol[k] == 0)
//              {
//                  degeneracy = 1;
//                  //fprintf(stderr, "DEGENERACY\n");
//              }

                //fprintf(stderr, "r=%i  tinc = %.15e, col[%i]=%.15e bcol[%i]=%.15e\n", r, tinc, k, col[k], k, bcol[k]);
                if (!degeneracy)
                {
                    assert(bcol[k] != 0);
                    assert(tinc > 0); assert(!isinf(tinc));
                    //assert(tinc != prev_inc);
                    accept = first_time || (tinc < s.inc);
                }

                //--------------------------------------------------------------
                // Accept this pivot element if we haven't found anything yet or
                // if this entry is more restrictive than the previous one.
                //--------------------------------------------------------------

                if (degeneracy || accept) 
                {
                    s.l   = k;
                    s.r   = r;
                    s.inc = tinc;
                    s.piv = col[k];
                    s.objf_inc = tinc*col[0];

                    assert(s.piv != 0);
                    //assert(s.inc != 0);
                    assert(col[0] != 0);
                    //assert(s.objf_inc != 0);
                }

                if (degeneracy) break;

                first_time=0;
            }
        }

        //----------------------------------------------------------------------
        // If no negative column entry was found then the system must be
        // unbounded.
        //----------------------------------------------------------------------
        if (res == UNBOUNDED)
        {
            return res;
        }

        //fprintf(stderr, "- %.15e %.15e %i\n", s.objf_inc, t.objf_inc, res);
        int good_enough =  left[s.l] < 0;

//      if (phase == 2)
//      {
//          good_enough |= (left[s.l] > left[t.l]);
//      }

        int accept2 = 0;

        if (degeneracy)
        {
            if (t.l == 0)
                good_enough = 1;
            else
                good_enough = drand48() > 0.5;
            //degeneracy = 0;
        }


        if (good_enough || (s.objf_inc > t.objf_inc))
        {
//          if (fabs(s.objf_inc - t.objf_inc) < 1e-8)
//          {
//              if (s.piv < t.piv)
//              {
//                  continue;
//              }
//          }
            //fprintf(stderr, "ACCEPTING tinc=%.15e  %.20e  %.15e  objf_inc=%.15e\n", s.inc, s.piv, s.inc, s.objf_inc);
            t.l = s.l;
            t.r = s.r;
            t.inc = s.inc;
            t.piv = s.piv;
            t.objf_inc = s.objf_inc;
            assert(t.piv != 0);

            if (good_enough)
                break;
        }
    }

    //--------------------------------------------------------------------------
    // If no positive entry was found in the first row then there is nothing 
    // to do.
    //--------------------------------------------------------------------------
    if (res == NOPIVOT)
        return res;

    DBG(2) fprintf(stderr, "ACCEPTING inc=%.15e\n", t.inc);
    //fprintf(stderr, "< choosePivot() %i %i %e %e\n", lpiv, rpiv, piv, inc);

    assert(t.piv != 0);

    *lpiv0 = t.l;
    *rpiv0 = t.r;
    *piv0  = t.piv;
    //fprintf(stderr, "< choosePivot() %i %i %e %e\n", t.l, t.r, t.piv, t.inc);
    return res;
}

#endif
#endif

int32_t choose_pivot1(matrix_t *tabl, int32_t *left, int32_t *right, long L, long R,
                      int32_t *lpiv0, int32_t *rpiv0, dble_t *piv0)
{ 
    int i;
    int32_t k, r;
    dble_t * restrict bcol = &tabl->data[0];

    dble_t tinc;
    dble_t * restrict col;

    DBG(3) fprintf(stderr, "> choosePivot()\n");

//  #pragma omp parallel for  \
//          private(col, l, cleft, cpiv, cinc, k, tinc, accept) \
//          shared(bcol, t, L, tabl)

    int32_t *Lvalid = malloc(L * sizeof(*Lvalid));

    int nvalid=0;
    dble_t min_tinc=1e10; 
    int32_t min_k = -1;
    int32_t l=0;
    dble_t cleft = 0;
    dble_t cinc = 0;
    int32_t accept = 0;

    {
redo:
#if 1
    //--------------------------------------------------------------------------
    // Choose a random columns from the right hand variables
    //--------------------------------------------------------------------------
    r = (int)(drand48() * R) + 1;
    assert(r >= 1);
    assert(r <= R);
    col = &tabl->data[r * tabl->rows + 0];

#else

    for (r=1; r <= R; r++)
    {
        col = &tabl->data[r * tabl->rows + 0];
        if (col[0] <= 0) break;
    }
    assert(r <= R);
#endif


    DBG(2) fprintf(stderr, "r=%i %i\n", r, R+1);

    nvalid=0;
    min_tinc=1e10; 
    min_k = -1;
    l=0;
    cleft = 0;
    cinc = 0;
    accept = 0;

    int first_time = 1;

    //--------------------------------------------------------------------------
    // Now find the left hand variable which limits the move to the next vertex
    //--------------------------------------------------------------------------
    fprintf(stderr, "*************************************\n");
    for (k=1; k <= L; k++) 
    { 
        if (col[k] >= 0) continue;
        //if (col[k] >= -1e-10) continue;

        DBG(3) fprintf(stderr, "%e %e\n", bcol[k], col[k]);
        tinc = fabs(bcol[k]/col[k]);
        //tinc = -bcol[k] * col[0]/col[k];

        //fprintf(stderr, "tinc %g\n", tinc);

        if (0) //tinc < 1e-9)
        {
            fprintf(stderr, "*************************************\n");
            fprintf(stderr, "*************************************\n");
            fprintf(stderr, "*************************************\n");
            fprintf(stderr, "tinc rejected  %20g %20g %20g\n", tinc, bcol[k], col[k]);
            fprintf(stderr, "*************************************\n");
            fprintf(stderr, "*************************************\n");
            fprintf(stderr, "*************************************\n");
            goto redo;
        }

        if (first_time)
        {
            accept = 1;
            first_time = 0;
        }
        else
        {
            accept = tinc < min_tinc;
        }
#if 0
        else if (ABS(tinc-cinc) >= EPS) 
        {
            accept = (tinc < cinc) + 8;
        }
        else 
        { 
            if (left[k] > 0 && cleft > 0)  
                accept = (left[k] > cleft) + 2;
            else 
                accept = (left[k] < cleft) + 4;
        }

        if ((accept&1)==0) continue; // && tinc > 0)
#endif

        if (accept)
        {
            min_tinc = tinc;
            min_k = k;
            //fprintf(stderr, "@@@@\n");
        }

        cleft = left[k]; 
        cinc  = tinc;

//      if (col[k] < 0)
//      {
//          fprintf(stderr, "valid col %i %e\n", k, col[k]);
//          Lvalid[nvalid] = k;
//          nvalid++;
//      }
    }
    }


    int32_t res; 

    if (min_k == -1)
    {
        res = UNBOUNDED;
        *lpiv0 = -1;
        *rpiv0 = -1;
        *piv0  = -1;
    }
    else
    {
//      fprintf(stderr, "nvalid = %i, L=%i\n", nvalid, L);
//      for (i=0; i < nvalid; i++)
//          fprintf(stderr, "Lvalid[%i] = %i\n", i, Lvalid[i]);
//      res = FOUND_PIVOT;

//      k = (int)(drand48() * nvalid);
//      fprintf(stderr, "k0 %i\n", k);
//      //k = 3;
//      assert(k >= 0);
//      assert(k < nvalid);
//      k = Lvalid[k];
//      assert(k >= 1);
//      assert(k <= L);
//      fprintf(stderr, "k1 %i\n", min_k);


        tinc = fabs(bcol[min_k]/col[min_k]);
        DBG(1) assert(!isinf(tinc));

        //fprintf(stderr, "tinc accepted  %20g\n", tinc);

        fprintf(stderr, "ACCEPTING tinc=%.15e\n", tinc);
        DBG(2) fprintf(stderr, "< choosePivot() %i %i %e %e %e\n", min_k, r, bcol[min_k], col[min_k], tinc);

        assert(col[min_k] < 0);

        *lpiv0 = min_k;
        *rpiv0 = r;
        *piv0  = col[min_k];
        res = FOUND_PIVOT;
    }

    free(Lvalid);
    return res;
}

char buf[100];
void periodic_report(int sig)
{
    assert(sig == SIGALRM);
    sprintf(buf, "\riter %8i  objf=%.8e  Z=%i  [nthreads=%i]", report.step, report.obj_val, report.Z, report.nthreads);
    write(STDERR_FILENO, buf, sizeof(buf));
    fsync(STDERR_FILENO);
    alarm(1);
}

PyObject *set_rnd_cseed(PyObject *self, PyObject *args)
{
    if (args == Py_None)
    {
        srand48(time(NULL));
    }
    else
    {
        srand48(PyInt_AsLong(args));
    }

    return Py_None;
}

/*==========================================================================*/
/* pivot                                                                    */
/*==========================================================================*/
PyObject *samplex_pivot(PyObject *self, PyObject *args)
{
    int32_t i,j;
    matrix_t tabl;

    PyObject *o = args;
    DBG(3) fprintf(stderr, "5> pivot()\n");

    long L = PyInt_AsLong(PyObject_GetAttrString(o, "nLeft")); /* # of constraints (rows)    */
    long R = PyInt_AsLong(PyObject_GetAttrString(o, "nRight")); /* # of variables   (columns) */
    long Z = PyInt_AsLong(PyObject_GetAttrString(o, "nTemp"));

    PyObject *lhv = PyObject_GetAttrString(o, "lhv");
    PyObject *rhv = PyObject_GetAttrString(o, "rhv");

    long nthreads = PyInt_AsLong(PyObject_GetAttrString(o, "nthreads"));
    if (nthreads < 1) nthreads = 1;

    int32_t *left  = (int32_t *)PyArray_DATA(lhv), 
            *right = (int32_t *)PyArray_DATA(rhv);


    int32_t lpiv,    /* Pivot row              */
            rpiv;    /* Pivot column           */
    dble_t piv;     /* Value of pivot element */

#if 0
        col = &(tabl.data[0 * tabl.rows + 0]);
        for (i=0; i <= L; i++)
            fprintf(stderr, "%.4f ", col[i]);
        fprintf(stderr, "\n");
#endif

    int32_t ret, n;



    /* Remember, this is in FORTRAN order */
    PyObject *data = PyObject_GetAttrString(o, "data");
    assert(PyArray_CHKFLAGS(data, NPY_F_CONTIGUOUS));
    assert(PyArray_CHKFLAGS(data, NPY_FORTRAN));
    tabl.data = (dble_t *)PyArray_DATA(data);
    tabl.rows = PyArray_DIM(data,0);
    tabl.cols = PyArray_DIM(data,1);

#if 0
    fprintf(stderr, "tabl dims = %i : %ix%i\n"
                    "L=%i R=%i Z=%i\n"
                    "len(left)=%i, len(right)=%i\n"
                    "nthreads=%i\n", 
                    PyArray_NDIM(data), tabl.cols, tabl.rows,
                    L, R, Z,
                    PyArray_DIM(lhv,0),
                    PyArray_DIM(rhv,0),
                    T
                    );
#endif


#if WITH_GOOGLE_PROFILER
    ProfilerStart("googperf.out");
#endif

    double stime = CPUTIME;

    signal(SIGALRM, periodic_report);
    alarm(1);

    Py_BEGIN_ALLOW_THREADS

    int Zorig = Z;

//  fprintf(stderr, "--------\n");
//  fprintf(stderr, "Z = %i\n", Z);
//  fprintf(stderr, "--------\n");

    //omp_set_num_threads(nthreads);

    int nsteps = 1000 * drand48();

    for (n=0;; n++)
    {
        if (Z == 0 && n >= nsteps) 
        {
            ret = NOPIVOT;
            break;
        }

        report.step     = n;
        report.obj_val  = tabl.data[0];
        report.nthreads = 0;
        report.Z = Z;

        ret = choose_pivot0(&tabl, left, right, L, R, &lpiv, &rpiv, &piv, 1+(Z==0));

        if (ret != FOUND_PIVOT) break;

        //------------------------------------------------------------------
        // Actual pivot
        //------------------------------------------------------------------
        if (Z == 0)
            doPivot0(&tabl, L, R, piv, lpiv, rpiv, 0);
        else
            doPivot0(&tabl, L, R, piv, lpiv, rpiv, 0);


        //----------------------------------------------------------------------
        // Swap left and right variables.
        //----------------------------------------------------------------------
        int32_t lq = left[lpiv];
        int32_t rq = right[rpiv];

        left[lpiv]  = rq;
        right[rpiv] = lq;

        if (lq < 0)
        { 
            //------------------------------------------------------------------
            // Remove the pivot column
            //------------------------------------------------------------------
            memmove(right+rpiv+0, 
                    right+rpiv+1, 
                    sizeof(*right)*(R-rpiv)); /* (R+1)-(rpiv+1) */

            memmove(tabl.data + (rpiv+0)*tabl.rows, 
                    tabl.data + (rpiv+1)*tabl.rows,
                    sizeof(*tabl.data) * (R-rpiv)*tabl.rows);

            Z--; 
            R--;
            need_assign_pivot_threads = 1;
            DBG(1) fprintf(stderr, "\nREMOVED %i Z=%ld R=%ld\n\n", rpiv, Z,R);
        }


        DBG(2)
        {
            for (i=0; i <= R; i++)
            {
                dble_t *col = &(tabl.data[i * tabl.rows + 0]);
                for (j=0; j <= L; j++)
                {
                    if (ABS(ABS(col[j]) - 22.00601215427127) < 1e-3)
                    {
                        fprintf(stderr, "+++++ i=%i j=%i  col[j]=%f\n", i, j, col[j]);
                        //assert(0);
                    }
                }
            }
        }

        if (Z==0) 
        {
            ret = FEASIBLE;
            break;
            //if (Zorig != 0) break;
        }
    }

#if WITH_GOOGLE_PROFILER
    ProfilerStop();
#endif

    Py_END_ALLOW_THREADS

    double etime = CPUTIME;

    alarm(0);
    signal(SIGALRM, SIG_DFL);

    //fprintf(stderr, "\rtime: %4.2f CPU seconds. %39c\n", (etime-stime), ' ');

    PyObject_SetAttrString(o, "nRight", PyInt_FromLong(R));
    PyObject_SetAttrString(o, "nTemp", PyInt_FromLong(Z));

    return PyInt_FromLong(ret);
}

void doPivot0X(
    matrix_t * restrict tabl,
    long L, const long R,
    dble_t piv, 
    int32_t lpiv, const int32_t rpiv)
{

    //DBG(3) fprintf(stderr, "> doPivot()\n");
    //DBG(3) fprintf(stderr, "doPivot called: %i %i L=%ld R=%ld rpiv=%i lpiv=%i\n", start, end, L, R, rpiv, lpiv);

    int32_t r;

    int32_t i;     
    dble_t * restrict col;
    dble_t col_lpiv;
    dble_t xx;

    dble_t * restrict pcol = tabl->data + (rpiv * tabl->rows);

//  #pragma omp parallel for \
//          private(i, col, col_lpiv, xx) \
//          shared(pcol, piv, L, tabl, lpiv)
    for (r=0; r <= R; ++r)
    {
        if (r == rpiv) continue;

        col      = tabl->data + (r * tabl->rows); 
        col_lpiv = col[lpiv];     
        xx       = col_lpiv / piv;  

        if (ABS(xx) >= SML)    
        {
            for (i=0; i <= L; i++) 
                col[i] -= pcol[i] * xx;  
        }
        else   
        {
            for (i=0; i <= L; i++) 
                col[i] -= (pcol[i] * col_lpiv) / piv;   
        }

        col[lpiv] = -xx;   
    }

    //#pragma omp parallel for shared(pcol, piv, L)
    for (i=0; i <= L; i++)
        pcol[i] /= piv;
    pcol[lpiv] = 1.0 / piv;

    DBG(1)
    {
        int f=0;
        for (i=1; i <= L; i++)
        {
            if (tabl->data[i] <= -SML)
            {
                fprintf(stderr, "%e\n", tabl->data[i]);
                f = 1;
            }
        }
        assert(f==0);
    }


    //DBG(3) fprintf(stderr, "< doPivot()\n");
}

#if 0
void doPivot0(
    matrix_t * restrict tabl,
    long L, const long R,
    dble_t piv, 
    int32_t lpiv, const int32_t rpiv,
    dble_t PREC)
{

    //DBG(3) fprintf(stderr, "> doPivot()\n");
    //DBG(3) fprintf(stderr, "doPivot called: %i %i L=%ld R=%ld rpiv=%i lpiv=%i\n", start, end, L, R, rpiv, lpiv);

    int32_t r;

    int32_t i;     
    dble_t * restrict col;
    dble_t col_lpiv;
    dble_t xx;

    dble_t * restrict pcol = tabl->data + (rpiv * tabl->rows);

//#define PREC 0.0
//#define PREC 1e-12

//  #pragma omp parallel for \
//          private(i, col, col_lpiv, xx) \
//          shared(pcol, piv, L, tabl, lpiv)
    for (r=0; r <= R; ++r)
    {
        if (r == rpiv) continue;

        col      = tabl->data + (r * tabl->rows); 
        col_lpiv = col[lpiv];     
        xx       = col_lpiv / piv;  

        if (r==-1)
        {
        for (i=0; i <= L; i++) 
        {
            fprintf(stderr, "%f %f %f %f %f %f || \n", col[i], pcol[i], xx, col_lpiv, piv, col[i] - pcol[i]*xx);
            col[i] -= pcol[i] * xx;  
            col[i] *= fabs(col[i]) > PREC;
        }
        }
        else
        for (i=0; i <= L; i++) 
        {
            dble_t t = fma(-pcol[i],xx, col[i]);

//          if (r==0) fprintf(stderr, "% .20e - % e * % e / % e= % .20e \n", 
//              col[i], pcol[i],col_lpiv, piv, t + (fma(-pcol[i],xx, col[i]) - t));

            col[i] = t + (fma(-pcol[i],xx, col[i]) - t);

            //col[i] -= pcol[i] * xx;  

            col[i] *= fabs(col[i]) > PREC;

        }

        //col[lpiv] = fabs(xx);
        col[lpiv] = -xx;   
        col[lpiv] *= fabs(col[lpiv]) > PREC;
    }

    //#pragma omp parallel for shared(pcol, piv, L)
    for (i=0; i <= L; i++)
    {
        pcol[i] /= piv;
        pcol[i] *= fabs(pcol[i]) > PREC;
    }
    pcol[lpiv] = 1.0 / piv;
    pcol[lpiv] *= fabs(pcol[lpiv]) > PREC;

    DBG(1)
    {
        int f=0;
        for (i=1; i <= L; i++)
        {
            if (tabl->data[i] < 0
            || (fabs(tabl->data[i]) > 0 && fabs(tabl->data[i]) < 1e-15)) //-SML)
            {
                fprintf(stderr, "%e\n", tabl->data[i]);
                f = 1;
            }
        }
        assert(f==0);
    }

    //fprintf(stderr, "ending doPivot. obj = %g\n", tabl->data[0]);

    //DBG(3) fprintf(stderr, "< doPivot()\n");
}
#endif
void doPivot0(
    matrix_t * restrict tabl,
    long L, const long R,
    dble_t piv, 
    int32_t lpiv, const int32_t rpiv,
    dble_t PREC)
{
    int32_t r, i;

    dble_t * restrict col;
    dble_t col_lpiv;
    dble_t xx;

    dble_t * restrict pcol = tabl->data + (rpiv * tabl->rows);

    for (r=0; r <= R; ++r)
    {
        if (r == rpiv) continue;

        col      = tabl->data + (r * tabl->rows); 
        col_lpiv = col[lpiv];     
        xx       = -col_lpiv / piv;  

        //assert(fabs(col_lpiv) > 1e-14);
        //col_lpiv *= fabs(col_lpiv) > 1e-14;

        for (i=0; i <= L; i++) 
        {
            //dble_t t = fma(pcol[i],xx, col[i]);
            //col[i] = t; // + (fma(pcol[i],xx, col[i]) - t);

            //col[i] = (-(pcol[i] * col_lpiv) + piv*col[i]) / piv;
            dble_t x = -(pcol[i] * col_lpiv) / piv;
            dble_t y = col[i];


            col[i] = x + y;
            col[i] *= fabs(col[i]) > PREC;

//          if (fabs(x-y) <= 1e-16 && x != y && x != 0 && y != 0)
//          {
//              fprintf(stderr, "%.15e %.15e\n", x,y);
//              assert (fabs(x-y) > 1e-16);
//          }
        }

        col[lpiv] = xx;   
        col[lpiv] *= fabs(col[lpiv]) > PREC;
    }

    for (i=0; i <= L; i++)
    {
        pcol[i] /= piv;
        pcol[i] *= fabs(pcol[i]) > PREC;
    }
    pcol[lpiv] = 1.0 / piv;
    pcol[lpiv] *= fabs(pcol[lpiv]) > PREC;

    DBG(1)
    {
        int f=0;
        for (i=1; i <= L; i++)
        {
            if (tabl->data[i] < 0
            || (fabs(tabl->data[i]) > 0 && fabs(tabl->data[i]) <= PREC)
            || isinf(tabl->data[i])
            ) //-SML)
            {
                fprintf(stderr, "%e\n", tabl->data[i]);
                f = 1;
            }
        }
        //fprintf(stderr, "piv=%e\n", piv);
        assert(f==0);
    }
}

//******************************************************************************
//*                                                                            *
//* RESTART.C                                                                  *
//*                                                                            *
//* WRITES RESTART FILE, READS RESTART FILE AND MAKES FUNCTION CALLS TO        *
//* INITIALIZE GRID, SET P AND U, COMPUTE FOUR-VECTORS AND APPLY BOUNDS        *
//*                                                                            *
//******************************************************************************

//include header files
#include "decs.h"

//include hdf5 header files
#include "hdf5_utils.h"

//include extra header files
#include <sys/stat.h>
#include <ctype.h>

// positron //
#include "positrons.h"

// declare variables
static int restart_id = 0;

// Declare known sizes for outputting primitives
static hsize_t fdims[] = {NVAR, N3TOT, N2TOT, N1TOT};
static hsize_t fcount[] = {NVAR, N3, N2, N1};
static hsize_t mdims[] = {NVAR, N3+2*NG, N2+2*NG, N1+2*NG};
static hsize_t mstart[] = {0, NG, NG, NG};

//******************************************************************************

void restart_write(struct FluidState *S) 
{
  restart_write_backend(S, IO_REGULAR);
}

//******************************************************************************

// write out restart file
void restart_write_backend(struct FluidState *S, int type)
{
  // count time
  timer_start(TIMER_RESTART);

  // Keep track of our own index
  restart_id++;

  // read restart file
  char fname[STRLEN];
  if (type == IO_REGULAR) {
    sprintf(fname, "restarts/restart_%08d.h5", restart_id);
  } else if (type == IO_ABORT) {
    sprintf(fname, "restarts/restart_abort.h5");
  }

  //create hdf5 file
  hdf5_create(fname);

  // Write header and primitive values all to root
  hdf5_set_directory("/");

  //hdf5 settings
  hid_t string_type = hdf5_make_str_type(20);
  hdf5_write_single_val(VERSION, "version", string_type);

  // Write grid size to check/error on import of wrong grid
  int n1 = N1TOT, n2 = N2TOT, n3 = N3TOT;
  hdf5_write_single_val(&n1, "n1", H5T_STD_I32LE);
  hdf5_write_single_val(&n2, "n2", H5T_STD_I32LE);
  hdf5_write_single_val(&n3, "n3", H5T_STD_I32LE);

  // time, number of step, final time, gas constants
  hdf5_write_single_val(&t, "t", H5T_IEEE_F64LE);
  hdf5_write_single_val(&nstep, "nstep", H5T_STD_I32LE);
  hdf5_write_single_val(&tf, "tf", H5T_IEEE_F64LE);
  hdf5_write_single_val(&gam, "gam", H5T_IEEE_F64LE);

  // electronic variables
  #if ELECTRONS
  hdf5_write_single_val(&game, "game", H5T_IEEE_F64LE);
  hdf5_write_single_val(&gamp, "gamp", H5T_IEEE_F64LE);
  hdf5_write_single_val(&fel0, "fel0", H5T_IEEE_F64LE);
  #endif

  // dt, cfl, and all that ...
  hdf5_write_single_val(&cour, "cour", H5T_IEEE_F64LE);
  hdf5_write_single_val(&DTd, "DTd", H5T_IEEE_F64LE);
  hdf5_write_single_val(&DTf, "DTf", H5T_IEEE_F64LE);
  hdf5_write_single_val(&DTl, "DTl", H5T_IEEE_F64LE);
  hdf5_write_single_val(&DTr, "DTr", H5T_STD_I32LE);
  hdf5_write_single_val(&DTp, "DTp", H5T_STD_I32LE);
  hdf5_write_single_val(&restart_id, "restart_id", H5T_STD_I32LE);
  hdf5_write_single_val(&dump_cnt, "dump_cnt", H5T_STD_I32LE);
  hdf5_write_single_val(&dt, "dt", H5T_IEEE_F64LE);

  // grid variables
#if METRIC == MKS
  hdf5_write_single_val(&Rin, "Rin", H5T_IEEE_F64LE);
  hdf5_write_single_val(&Rout, "Rout", H5T_IEEE_F64LE);
  hdf5_write_single_val(&a, "a", H5T_IEEE_F64LE);
  hdf5_write_single_val(&hslope, "hslope", H5T_IEEE_F64LE);
  hdf5_write_single_val(&Rhor, "Rhor", H5T_IEEE_F64LE);
#else
  hdf5_write_single_val(&x1Min, "x1Min", H5T_IEEE_F64LE);
  hdf5_write_single_val(&x1Max, "x1Max", H5T_IEEE_F64LE);
  hdf5_write_single_val(&x2Min, "x2Min", H5T_IEEE_F64LE);
  hdf5_write_single_val(&x2Max, "x2Max", H5T_IEEE_F64LE);
  hdf5_write_single_val(&x3Min, "x3Min", H5T_IEEE_F64LE);
  hdf5_write_single_val(&x3Max, "x3Max", H5T_IEEE_F64LE);
#endif

  /*--------------------------------------------------*/

  // Leon's patch, mass unit and black hole mass //
  hdf5_write_single_val(&mbh, "mbh", H5T_IEEE_F64LE);
  hdf5_write_single_val(&eta_edd, "eta_edd", H5T_IEEE_F64LE);

  /*--------------------------------------------------*/

  //////////////////////////////////////////////////////////////////////////////////////////////
  // TODO these are unused.  Stop writing them when backward compatibility will not be an issue
  //////////////////////////////////////////////////////////////////////////////////////////////
  hdf5_write_single_val(&tdump, "tdump", H5T_IEEE_F64LE);
  hdf5_write_single_val(&tlog, "tlog", H5T_IEEE_F64LE);

  // Write data
  // As this is not packed, the read_restart_prims fn is different per-code
  hsize_t fstart[] = {0, global_start[2], global_start[1], global_start[0]};
  hdf5_write_array(S->P, "p", 4, fdims, fstart, fcount, mdims, mstart, H5T_IEEE_F64LE);
  
  //close hdf5 files
  hdf5_close();

  //mpi stuff
  if(mpi_io_proc()) {
    fprintf(stdout, "RESTART %s\n", fname);

    // Symlink when we're done writing (link to last good file)
    char fname_nofolder[80];
    sprintf(fname_nofolder, "restart_%08d.h5", restart_id);

    // Chained OS functions: switch to restart directory,
    // remove current link, link last file, switch back
    int errcode;
    errcode = chdir("restarts");
    if ( access("restart.last", F_OK) != -1 ) {
      errcode = errcode || remove("restart.last");
    }
    errcode = errcode || symlink(fname_nofolder, "restart.last");
    errcode = errcode || chdir("..");
    if(errcode != 0) {
      printf("Symlink failed: errno %d\n", errno);
      exit(-1);
    }
  }

  //count time
  timer_stop(TIMER_RESTART);
}

//******************************************************************************

//read restart files
void restart_read(char *fname, struct FluidState *S)
{
  //open hdft files
  hdf5_open(fname);

  // Read everything from root
  hdf5_set_directory("/");
  hid_t string_type = hdf5_make_str_type(20);
  char version[20];
  hdf5_read_single_val(version, "version", string_type);

  //mpi stuff
  if(mpi_io_proc()) {
    fprintf(stderr, "Restarting from %s, file version %s\n\n", fname, version);
  }

  // read n1, n2, n3
  int n1, n2, n3;
  hdf5_read_single_val(&n1, "n1", H5T_STD_I32LE);
  hdf5_read_single_val(&n2, "n2", H5T_STD_I32LE);
  hdf5_read_single_val(&n3, "n3", H5T_STD_I32LE);
  if(n1 != N1TOT || n2 != N2TOT || n3 != N3TOT) {
    if (mpi_io_proc()) fprintf(stderr, "Restart file is wrong size!\n");
    exit(-1);
  }

  //read time, number of step, gas constant
  hdf5_read_single_val(&t, "t", H5T_IEEE_F64LE);
  hdf5_read_single_val(&nstep, "nstep", H5T_STD_I32LE);
  hdf5_read_single_val(&gam, "gam", H5T_IEEE_F64LE);

  //read electronic variables
  #if ELECTRONS
  hdf5_read_single_val(&game, "game", H5T_IEEE_F64LE);
  hdf5_read_single_val(&gamp, "gamp", H5T_IEEE_F64LE);
  hdf5_read_single_val(&fel0, "fel0", H5T_IEEE_F64LE);
  #endif

  ///////////////////////////////////////////////////////////////////////////////////////
  // I want to be able to change tf/cadences/courant mid-run
  // Hence we just pick these up from param.dat again unless we're testing the MHD modes
  // TODO include problem name in parameters.h
  ///////////////////////////////////////////////////////////////////////////////////////

  // read other essential run paraameters
if (METRIC != MKS) {
  hdf5_read_single_val(&tf, "tf", H5T_IEEE_F64LE);
  hdf5_read_single_val(&cour, "cour", H5T_IEEE_F64LE);
  hdf5_read_single_val(&DTd, "DTd", H5T_IEEE_F64LE);
  hdf5_read_single_val(&DTf, "DTf", H5T_IEEE_F64LE);
  hdf5_read_single_val(&DTl, "DTl", H5T_IEEE_F64LE);
  hdf5_read_single_val(&DTr, "DTr", H5T_STD_I32LE);
  hdf5_read_single_val(&DTp, "DTp", H5T_STD_I32LE);
}
  hdf5_read_single_val(&restart_id, "restart_id", H5T_STD_I32LE);
  hdf5_read_single_val(&dump_cnt, "dump_cnt", H5T_STD_I32LE);
  hdf5_read_single_val(&dt, "dt", H5T_IEEE_F64LE);
#if METRIC == MKS
  hdf5_read_single_val(&Rin, "Rin", H5T_IEEE_F64LE);
  hdf5_read_single_val(&Rout, "Rout", H5T_IEEE_F64LE);
  hdf5_read_single_val(&a, "a", H5T_IEEE_F64LE);
  hdf5_read_single_val(&hslope, "hslope", H5T_IEEE_F64LE);
  hdf5_read_single_val(&Rhor, "Rhor", H5T_IEEE_F64LE);
#else
  hdf5_read_single_val(&x1Min, "x1Min", H5T_IEEE_F64LE);
  hdf5_read_single_val(&x1Max, "x1Max", H5T_IEEE_F64LE);
  hdf5_read_single_val(&x2Min, "x2Min", H5T_IEEE_F64LE);
  hdf5_read_single_val(&x2Max, "x2Max", H5T_IEEE_F64LE);
  hdf5_read_single_val(&x3Min, "x3Min", H5T_IEEE_F64LE);
  hdf5_read_single_val(&x3Max, "x3Max", H5T_IEEE_F64LE);
#endif

  /*--------------------------------------------------*/

  // Leon's patch, mass unit and black hole mass //
  // However, if we want to over write these values, do not read in //
#if POSITRONS && !OVER_WRITE
  hdf5_read_single_val(&mbh, "mbh", H5T_IEEE_F64LE);
  hdf5_read_single_val(&eta_edd, "eta_edd", H5T_IEEE_F64LE);
#endif

/////////////////////////////////////////////////////////////
//  hdf5_read_single_val(&tdump, "tdump", H5T_IEEE_F64LE);
//  hdf5_read_single_val(&tlog, "tlog", H5T_IEEE_F64LE);
/////////////////////////////////////////////////////////////

  // Read data
  hsize_t fstart[] = {0, global_start[2], global_start[1], global_start[0]};
  hdf5_read_array(S->P, "p", 4, fdims, fstart, fcount, mdims, mstart, H5T_IEEE_F64LE);

  //close hdf5 file
  hdf5_close();

  //mpi stuff
  mpi_barrier();
}

//******************************************************************************

// initialize stuff after restart
int restart_init(struct GridGeom *G, struct FluidState *S)
{
  // print out restart file
  char fname[STRLEN];
  sprintf(fname, "restarts/restart.last");

  FILE *fp = fopen(fname,"rb");
  if (fp == NULL) {
    if (mpi_io_proc())
      fprintf(stdout, "No restart file: error %d\n\n", errno);
    return 0;
  }
  fclose(fp);

  //mpi stuff
  if (mpi_io_proc())
    fprintf(stdout, "Loading restart file %s\n\n", fname);

  //zero all arrays
  zero_arrays();

  //read files
  restart_read(fname, S);

  //initialize grids
  set_grid(G);

  // Calculate ucon, ucov, bcon, bcov, and conservative variables 
  get_state_vec(G, S, CENT, 0, N3 - 1, 0, N2 - 1, 0, N1 - 1);
  prim_to_flux_vec(G, S, 0, CENT, 0, N3 - 1, 0, N2 - 1, 0, N1 - 1, S->U);

  //boundary conditions
  set_bounds(G, S);

  return 1;
}


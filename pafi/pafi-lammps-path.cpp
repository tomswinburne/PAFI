#include "pafi.hpp"

int main(int narg, char **arg) {
  MPI_Init(&narg,&arg);
  int rank, nProcs;
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  MPI_Comm_size(MPI_COMM_WORLD,&nProcs);

  // Load input file
  Parser params("./config.xml");
  params.CoresPerWorker = nProcs;

  if(nProcs>params.CoresPerWorker) {
    if(rank==0) {
      std::cout<<"\n\n\n*****************************\n\n\n";
      std::cout<<"pafi-lammps-path should only be run with a single worker!\n";
      std::cout<<"\n\n\n*****************************\n\n\n";
    }
    exit(-1);
  }



  // Find fresh dump folder name - no nice solution here as
  // directory creation requires platform dependent features
  // which we omit for portability
  int *int_dump_suffix = new int[1];
  std::ofstream raw;
  std::string params_file = params.dump_dir+"/params_"+std::to_string(int_dump_suffix[0]);

  // try to write to a file with a unique suffix

  //if(rank==0)  std::cout<<params.welcome_message();


  const int nWorkers = nProcs / params.CoresPerWorker;
  const int instance = rank / params.CoresPerWorker;
  const int local_rank = rank % params.CoresPerWorker;
  const int nRes = 7; // TODO remove this
	const int nRepeats = params.nRepeats;



  params.seed(123);
  MPI_Comm instance_comm;
  MPI_Comm_split(MPI_COMM_WORLD,0,0,&instance_comm);

  Simulator sim(instance_comm,params,instance,nRes);
  if(!sim.has_pafi) {
    if(rank==0)
      std::cout<<"PAFI Error: missing USER-MISC package in LAMMPS"<<std::endl;
    exit(-1);
  }
  if(sim.error_count>0 && local_rank==0) std::cout<<sim.last_error()<<std::endl;

  auto cell = sim.getCellData();

  if(rank==0) {
    std::cout<<"Loaded input data of "<<sim.natoms<<" atoms\nSupercell Matrix:\n";
    for(int i=0;i<3;i++) {
      std::cout<<"\t";
      for(int j=0;j<3;j++) std::cout<<sim.pbc.cell[i][j]<<" ";
      std::cout<<"\n";
    }
    std::cout<<"\n\n";
  }

  sim.make_path(params.PathwayConfigurations);

  int fileindex=1;
  std::string cmd;
  double dr,E,nm,fE,fs;
  double *f;

  f = new double[3*sim.natoms];

  if (params.nPlanes>1) dr = (params.stopr-params.startr)/(double)(params.nPlanes-1);
  else dr = 0.1;

  if(rank==0) {
    std::cout<<"\n\nPath Loaded\n\n";
    std::cout<<std::setprecision(15)<<"r index dE |tangent| |force|"<<std::endl;
  }

  for (double r = params.startr; r <= params.stopr+0.5*dr; r += dr ) {

    sim.populate(r,nm,0.0);

    sim.run_script("PreRun");  // Stress Fixes

    // pafi fix
    cmd = "run 0\n"; // to ensure the PreRun script is executed
    cmd += "run 0\nfix hp all pafi __pafipath 0.0 ";
    cmd += params.parameters["Friction"]+" ";
    cmd += params.seed_str()+" overdamped 1 com 0\n run 0";
    sim.run_commands(cmd);

    if(params.preMin) {
      #ifdef VERBOSE
      if(rank==0)
        std::cout<<"LAMMPSSimulator.populate(): minimizing"<<std::endl;
      #endif
      cmd = "min_style fire\n minimize 0 0.01 ";
      cmd += params.parameters["MinSteps"]+" "+params.parameters["MinSteps"];
      sim.run_commands(cmd);
    }

    cmd = "run 0";
    sim.run_commands(cmd);

    E = sim.getForceEnergy(f);

    fs=0.0;
    for(int i=0; i<3*sim.natoms; i++) fs += f[i]*f[i];

    if(r==params.startr) fE = E;
    if(rank==0)
      std::cout<<std::setprecision(15)<<r<<" "<<fileindex<<" "<<E-fE<<" "<<nm<<" "<<sqrt(fs)<<std::endl;
    sim.lammps_dump_path("dumps/pafipath."+std::to_string(fileindex)+".data",r);
    fileindex++;

    cmd = "unfix hp";
    sim.run_commands(cmd);
    sim.run_script("PostRun");  // Stress Fixes
  }


  // close down LAMMPS instances
  sim.close();

  MPI_Finalize();

}

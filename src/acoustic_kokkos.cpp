#include<cmath>
#include<cstdio>
#include<cstdlib>
#include<cstring>
#include<sys/time.h>
#include <vector>
// #include <fstream>
// #include <iostream>
// #include<iomanip>

#include "real_type.h"
#include "params.h"
#include "usekokkos.hpp"
#include "useserial.hpp"
// vel_module
#include "vel_module.hpp"
#include "src_module.hpp"
// utils
#include "utils.hpp"

// ===============================================================
// ===============================================================
// ===============================================================
int main(int argc, char* argv[]) {

    // Parameters
  
    int n[2] = {201,801};       // 3d array linear size 
    int nt = 1600;  				  // number of time steps
    int nteams = 4;   // default number of teams (for TeamPolicy)
    int ghost = 1;    // default ghost nodes
    int ns = 1;  	// #source number
    char* fname;
    real_t dt = 0.002;
    real_t dx = 20;
    int nm[2];
    int fileIndex;
    char sprintfBuffer[500];
    int fileWriteTimestepInterval= 4;
    int interval=1;
    int BC1[2]={1,2}; //BC: 0 (rigid); 1 (free surface); 2 (absorbing);
    int BC2[2]={2,2}; //BC: 0 (rigid); 1 (free surface); 2 (absorbing);
  
  
    // // Read command line arguments
    // for(int i=0; i<argc; i++) {
    //   	if( strcmp(argv[i], "-nx") == 0) {
    //   	  n[0] = atoi(argv[++i]);
    //   	} else if( strcmp(argv[i], "-ny") == 0) {
    //   	  n[1] = atoi(argv[++i]);
    //   	} else if( strcmp(argv[i], "-ns") == 0) {
    //   	  ns = atoi(argv[++i]);
    //   	} else if( strcmp(argv[i], "-ghost") == 0) {
    //   	  ghost = atoi(argv[++i]);
    //   	} else if( strcmp(argv[i], "-nt") == 0) {
    //   	  nt = atoi(argv[++i]);
    //   	} else if( strcmp(argv[i], "-dt") == 0) {
    //   	  dt = atof(argv[++i]);
    //   	} else if( strcmp(argv[i], "-dx") == 0) {
    //   	  dx = atof(argv[++i]);
    //   	} else if( strcmp(argv[i], "-nteams") == 0) {
    //   	  nteams = atoi(argv[++i]);
    //   	} else if( strcmp(argv[i], "-dt") == 0) {
    //   	  dt = atof(argv[++i]);
    //   	} else if( strcmp(argv[i], "-velfile") == 0) {
    //   	  fname = &(argv[++i][0]);
    //   	} else if( (strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "-help") == 0)) {
    //   	  printf("STENCIL 3D Options:\n");
    //   	  printf("  -velfile <char>:  vel input filename (default: 'vel.bin')\n");
    //   	  printf("  -nx <int>:        dimension of x axis (default: 256)\n");
    //   	  printf("  -ny <int>:        dimension of y axis (default: 256)\n");
    //   	  printf("  -nt <int>:   	  number of time steps (default: 1000)\n");
    //   	  printf("  -dt <real_t>:     discretized time step (default: 0.01)\n");
    //   	  printf("  -ghost <int>:     ghost layer node number (default: 1)\n");
    //   	  printf("  -nteams <int>:    number of teams (only for version 5bis, default is 4)\n");
    //   	  return EXIT_SUCCESS;
    //   	}
    // }
    
    // update some constants
    nm[0] = n[0] + 2 * ghost;
    nm[1] = n[1] + 2 * ghost;
  

    //Parameter space
    Params par(nm[0],nm[1],ns,dt,dx);
  
    //Generate velocity structure
    vel_module vm(par);

    // fname = (char*)"in/vel_z6.25m_x12.5m_exact.segy";
    fname = (char*)"in/randomvel.bin";
    vm.readbin_vel(fname);
  
    //Generate source 
    src_module src(par,1);
  
    //Initialize Kokkos
    Kokkos::initialize(argc,argv);
    {
    // initiate arrays
    // Allocate Views
    // DataArray u("u_0", par.NX*par.NY); // Displacement in device
    // DataArray::HostMirror u_host = Kokkos::create_mirror_view(u); //Host mirror
  
    // kokkos_config();
  
    printf("\n\nParallel execution with kokkos starts.\n");
    printf("--------------------------------------------\n\n");
    DataContextKokkos datakokkos(par);
    DataContext       dataserial(par);
  
    //deep copy CFL2 from Host to Device
    for (int i=0; i<par.NX*par.NY; ++i){
    	datakokkos.c2_h(i) = vm.courant2[i];
    }
    Kokkos::deep_copy(datakokkos.c2_d,datakokkos.c2_h);

    //deep copy CFL2 from Host to Device
    for (int i=0; i<par.NX*par.NY; ++i){
    	datakokkos.c_h(i) = vm.courant[i];
    }
    Kokkos::deep_copy(datakokkos.c_d,datakokkos.c_h);
  
    


    // ---------------- Initiation displacement dataset ------------
    Kokkos::parallel_for( par.NX*par.NY, KOKKOS_LAMBDA(const int index) {
    	datakokkos.u0_d(index) = 0.;
    	datakokkos.u1_d(index) = 0.;
    	datakokkos.u2_d(index) = 0.;
    });

    Kokkos::fence();


    for (int i=0; i<par.NX*par.NY; ++i){
    	datakokkos.u_h(i) = 0.;
    	dataserial.u0[i] = 0.;
    	dataserial.u1[i] = 0.;
    	dataserial.u2[i] = 0.;
    } 




    fileIndex = 0;
    for (int it=0; it < nt; ++it){
    	// computing time
    	par.Time = it * par.dt;

   



    	//------------------  Solver ---------------------------------------

    	Kokkos::parallel_for( par.NX*par.NY, KOKKOS_LAMBDA(const int index) {  
    		int i,j;
			index2coord(index,i,j,par.NX,par.NY);

			const int ij = INDEX(i,j,par.NX,par.NY);
			const int ip1j = INDEX(i+1,j,par.NX,par.NY);
			const int im1j = INDEX(i-1,j,par.NX,par.NY);
			const int ijp1 = INDEX(i,j+1,par.NX,par.NY);
			const int ijm1 = INDEX(i,j-1,par.NX,par.NY);

			// if (index == 100) printf("%03d %03d %010d vs ",i,j,ij);
			if (i>0 and i<par.NX-1 and j>0 and j<par.NY-1 ){
				datakokkos.u2_d(ij) = (2-4*datakokkos.c2_d(ij))*datakokkos.u1_d(ij)+
				datakokkos.c2_d(ij)*
				(datakokkos.u1_d(ip1j)+datakokkos.u1_d(im1j)+
				 datakokkos.u1_d(ijp1)+datakokkos.u1_d(ijm1))-datakokkos.u0_d(ij);
			}

    	});


    	for (int i=1; i<par.NX-1; ++i){
    		for (int j=1; j<par.NY-1; ++j){
    			const int ij = INDEX(i,j,par.NX,par.NY);
				const int ip1j = INDEX(i+1,j,par.NX,par.NY);
				const int im1j = INDEX(i-1,j,par.NX,par.NY);
				const int ijp1 = INDEX(i,j+1,par.NX,par.NY);
				const int ijm1 = INDEX(i,j-1,par.NX,par.NY);

				dataserial.u2[ij] = (2-4*vm.courant2[ij])*dataserial.u1[ij]+
				vm.courant2[ij]*
				(dataserial.u1[ip1j]+dataserial.u1[im1j]+
				 dataserial.u1[ijp1]+dataserial.u1[ijm1])-dataserial.u0[ij];
    		}
    	}







    	//------------------  SOURCE  ---------------------------------------
    	//Implementation of adding sources
    	src.add_src(par.Time);
    	//deep copy n source from Host to Device
    	for (int i=0; i<par.NS; ++i){
    		datakokkos.src_h(i) = src.stf[i];
    		// if (it<3) printf("%d %f\n",it,src.stf[i]);
    	}

    	Kokkos::deep_copy(datakokkos.src_d,datakokkos.src_h);
    	Kokkos::fence();


    	for (int i=0; i<par.NS; ++i){
    		if (par.Time < src.tlen[i]) {
    			int ind = INDEX(src.isx[i],src.isy[i],par.NX,par.NY);

    			Kokkos::parallel_for( par.NX*par.NY, KOKKOS_LAMBDA(const int index) { 
    				if(index ==  ind) datakokkos.u2_d(index)=datakokkos.src_d(i);
    			});

    			int ind2 = INDEX(src.isx[i],src.isy[i],par.NX,par.NY);
    			dataserial.u2[ind2] = src.stf[i];
    			// printf("%d %d %d %d\n", src.isx[i],src.isy[i],ind,ind2);
    		}	
    	}







    	//------------ Boundary condition ----------------------------------
    	Kokkos::parallel_for( par.NX*par.NY, KOKKOS_LAMBDA(const int index) { 
    		int i,j,i2;
			index2coord(index,i,j,par.NX,par.NY);

			// // Absorbing boundary on the top edge (0-x)
			if (BC1[0]==2 and i==0) {
				i2=INDEX(1,j,par.NX,par.NY);
				const real_t c = datakokkos.c_d(i2);
				datakokkos.u2_d(index) = c*datakokkos.u1_d(i2)+
										(1-c)*datakokkos.u1_d(index);
			}

			// // Absorbing boundary on the bottom edge (x-1)
			if (BC2[0]==2 and i==par.NX-1) {
				i2=INDEX(par.NX-2,j,par.NX,par.NY);
				const real_t c = datakokkos.c_d(i2);
				datakokkos.u2_d(index) = c*datakokkos.u1_d(i2)+
										(1-c)*datakokkos.u1_d(index);
			}


			// // Absorbing boundary on the left edge (0-y)
			if (BC1[1]==2 and j==0) {
				i2=INDEX(i,1,par.NX,par.NY);
				const real_t c = datakokkos.c_d(i2);
				datakokkos.u2_d(index) = c*datakokkos.u1_d(i2)+
										(1-c)*datakokkos.u1_d(index);
			}

			// // Absorbing boundary on the right edge (y-1)
			if (BC2[1]==2 and j==par.NY-1) {
				i2=INDEX(i,par.NY-2,par.NX,par.NY);
				const real_t c = datakokkos.c_d(i2);
				datakokkos.u2_d(index) = c*datakokkos.u1_d(i2)+
										(1-c)*datakokkos.u1_d(index);
			}


			// below are free surface BC
			if (BC1[0]==1 and i==0) {
				i2=INDEX(1,j,par.NX,par.NY);
				datakokkos.u2_d(index) = datakokkos.u2_d(i2);
			}


			if (BC2[0]==1 and i==par.NX-1) {
				i2=INDEX(par.NX-2,j,par.NX,par.NY);
				datakokkos.u2_d(index) = datakokkos.u2_d(i2);
			}


			if (BC1[1]==1 and j==0) {
				i2=INDEX(i,1,par.NX,par.NY);
				datakokkos.u2_d(index) = datakokkos.u2_d(i2);
			}


			if (BC2[1]==1 and j==par.NY-1) {
				i2=INDEX(i,par.NY-2,par.NX,par.NY);
				datakokkos.u2_d(index) = datakokkos.u2_d(i2);
			}



    	});

    	for (int i=0; i<par.NX; ++i){
    		for (int j=0; j<par.NY; ++j){

    			if (BC1[0]==2 and i==0) {
    				const int ij = INDEX(i,j,par.NX,par.NY);
    				const int i2=INDEX(1,j,par.NX,par.NY);
    				const real_t c = vm.courant[i2];
    				dataserial.u2[ij] = c*dataserial.u1[i2]+(1-c)*dataserial.u1[ij];
    			}


    			if (BC1[1]==2 and i==par.NX-1) {
    				const int ij = INDEX(i,j,par.NX,par.NY);
    				const int i2=INDEX(par.NX-2,j,par.NX,par.NY);
    				const real_t c = vm.courant[i2];
    				dataserial.u2[ij] = c*dataserial.u1[i2]+(1-c)*dataserial.u1[ij];
    			}

    			if (BC2[0]==2 and j==0) {
    				const int ij = INDEX(i,j,par.NX,par.NY);
    				const int i2=INDEX(i,1,par.NX,par.NY);
    				const real_t c = vm.courant[i2];
    				dataserial.u2[ij] = c*dataserial.u1[i2]+(1-c)*dataserial.u1[ij];
    			}


    			if (BC2[1]==2 and j==par.NY-1) {
    				const int ij = INDEX(i,j,par.NX,par.NY);
    				const int i2=INDEX(i,par.NY-2,par.NX,par.NY);
    				const real_t c = vm.courant[i2];
    				dataserial.u2[ij] = c*dataserial.u1[i2]+(1-c)*dataserial.u1[ij];
    			}


    			if (BC1[0]==1 and i==0) {
    				const int ij = INDEX(i,j,par.NX,par.NY);
    				const int i2=INDEX(1,j,par.NX,par.NY);
    				dataserial.u2[ij] = dataserial.u2[i2];
    			}


    			if (BC2[0]==1 and i==par.NX-1) {
    				const int ij = INDEX(i,j,par.NX,par.NY);
    				const int i2=INDEX(par.NX-2,j,par.NX,par.NY);
    				dataserial.u2[ij] = dataserial.u2[i2];
    			}

    			if (BC1[1]==1 and j==0) {
    				const int ij = INDEX(i,j,par.NX,par.NY);
    				const int i2=INDEX(i,1,par.NX,par.NY);
    				dataserial.u2[ij] = dataserial.u2[i2];
    			}


    			if (BC2[1]==1 and j==par.NY-1) {
    				const int ij = INDEX(i,j,par.NX,par.NY);
    				const int i2=INDEX(i,par.NY-2,par.NX,par.NY);
    				dataserial.u2[ij] = dataserial.u2[i2];
    			}


    		}
    	}






    	//------------ Time Update -----------------------------------------
    	Kokkos::deep_copy(datakokkos.u0_d,datakokkos.u1_d);
    	Kokkos::deep_copy(datakokkos.u1_d,datakokkos.u2_d);


    	for (int i=0; i<par.NX*par.NY; ++i){
    		dataserial.u0[i] = dataserial.u1[i];
    		dataserial.u1[i] = dataserial.u2[i];
    	}



    	// ------------ output --------------------------------------------
    	if (it % fileWriteTimestepInterval == 0) {

    		
    		Kokkos::deep_copy(datakokkos.u_h, datakokkos.u1_d);

    		#ifdef DEBUG
    		for (int index=0; index<par.NX*par.NY; ++index){
    			if (abs(datakokkos.u_h(index)-dataserial.u1[index])>1e-5) 
    					printf("U1host= %f serial= %f\n",datakokkos.u_h(index),dataserial.u1[index]);
    		}
    		#endif


      		sprintf(sprintfBuffer, "out/Kokkos_%03u.csv", fileIndex);
      		FILE* file = fopen(sprintfBuffer, "w");


      		if (!file){
     			std::cout << "Couldn't write the file!\nPlease 'mkdir out/' and rerun" << std::endl;
    			return 0;
			}


      		sprintf(sprintfBuffer, "out/Serial_%03u.csv", fileIndex);
      		FILE* file2 = fopen(sprintfBuffer, "w");

      		int ind=0;
      		for (unsigned int i = 0; i < par.NX; i += interval) {
      			ind = INDEX(i,0,par.NX,par.NY);

        		fprintf(file, "%4.2f", datakokkos.u_h(ind));
        		fprintf(file2, "%4.2f", dataserial.u1[ind]);
        		for (unsigned int j = interval; j < par.NY; j += interval) {
        			ind = INDEX(i,j,par.NX,par.NY);
          			fprintf(file, ", %4.2f", datakokkos.u_h(ind));
          			fprintf(file2, ", %4.2f", dataserial.u1[ind]);

          
        		}
        		fprintf(file, "\n");
        		fprintf(file2, "\n");
      		}

      		fclose(file);
      		fclose(file2);
      		++fileIndex;
      	}

    }
  
  
 
    // Kokkos::parallel_for( par.NX*par.NY, KOKKOS_LAMBDA(const int index) {
    //   datakokkos.c2_d(index) +=datakokkos.u0_d(index);
    //   });
  
  
    }
      // Shutdown Kokkos
    Kokkos::finalize();
    printf("\n\n--------------------------------------------\n");
    printf("Parallel execution with kokkos ends.\n\n");
    return EXIT_SUCCESS;
}







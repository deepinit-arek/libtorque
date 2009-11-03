#include <string.h>
#include <libtorque/arch.h>
#include <libtorque/schedule.h>
#include <libtorque/x86cpuid.h>
#include <libtorque/topology.h>

static cpu_set_t validmap;			// affinityid_map validity map
static struct {
	unsigned apic;
	unsigned thread,core,package;
} cpu_map[CPU_SETSIZE];
static unsigned affinityid_map[CPU_SETSIZE];	// maps into the cpu desc table

// We must be currently pinned to the processor being associated
int associate_affinityid(unsigned aid,unsigned idx,unsigned apic,
				unsigned thread){
	if(aid >= sizeof(affinityid_map) / sizeof(*affinityid_map)){
		return -1;
	}
	if(CPU_ISSET(aid,&validmap)){
		return -1;
	}
	cpu_map[aid].thread = thread;
	cpu_map[aid].core = 0;
	cpu_map[aid].package = 0;
	cpu_map[aid].apic = apic;
	CPU_SET(aid,&validmap);
	affinityid_map[aid] = idx;
	return 0;
}

void reset_topology(void){
	CPU_ZERO(&validmap);
	memset(cpu_map,0,sizeof(cpu_map));
	memset(affinityid_map,0,sizeof(affinityid_map));
}

// development code
#include <stdio.h>

int print_topology(void){
   unsigned z;

   for(z = 0 ; z < sizeof(affinityid_map) / sizeof(*affinityid_map) ; ++z){
           if(CPU_ISSET(z,&validmap)){
		   const libtorque_cput *cpu;
		   uint32_t apic;

		   if((cpu = libtorque_cpu_getdesc(affinityid_map[z])) == NULL){
			   return -1;
		   }
		   apic = cpu_map[z].apic;
                   printf("Cpuset ID %u: Type %u, APIC ID 0x%08x (%u) SMT %u Core %u Package %u\n",z,
                           affinityid_map[z] + 1,apic,apic,cpu_map[z].thread,
			   cpu_map[z].core,cpu_map[z].package);
           }
   }
   return 0;
}

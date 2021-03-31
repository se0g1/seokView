#include "memCtlZoneCommand.h"
#include "memCtlCommand.h"
#include "../ktrr/ktrr_bypass_parameters.h"
#include "../kernel/kernel_memory.h"

bool zone_space(kaddr_t address)
{
	// printf("zone_map_min_address => 0x%llx\n",zone_map_min_addr);
	// printf("zone_map_max_address => 0x%llx\n",zone_map_max_addr);
	// printf("zone_base => 0x%llx\n",ADDRESS(zone_base));
	// printf("zone_metadata_region_min => 0x%llx\n",zone_metadata_region_min);
	// printf("zone_metadata_region_max => 0x%llx\n",zone_metadata_region_max);

	if(address < zone_map_min_addr || address > zone_map_max_addr)
	{
		printf("Not found address from zone\n");
		return false;
	}
	if(address >= zone_map_min_addr && address < zone_map_max_addr)
	{
		uint64_t page_index = ((address & ~(page_size - 1)) - zone_map_min_addr) / page_size;
		uint64_t page_meta = zone_metadata_region_min + page_index * 24;
		//printf("[+] page_index => 0x%llx\n",page_index);
		bool checkSafe = false;
		if( page_meta != 0)
		{
			// zone index & findzone 
			uint64_t value = 0;
			uint64_t zindex = page_meta+0x14;
			//printf("[-] zindex address => 0x%llx\n",zindex);
			checkSafe = safeacess(zindex);
			if(checkSafe){
				zindex = kernel_read16(zindex);
			}
			else if(!checkSafe) {
				printf("[+] zindex Error \n");
			}
			uint64_t findzone = ADDRESS(zone_base) + (zindex * 0x140);

			// zone name
			uint64_t zonename = findzone+0x120;
			char zonenamearray[30];
			char a[2];
			int cnt = 0;
			checkSafe = safeacess(zonename);
			printf("[+] zonename address => 0x%llx\n",zonename);
			
			if(checkSafe){
				bool ok = kernel_read(zonename, &value, sizeof(value));
				if (!ok) {
					return -1;
				}
			else if(!checkSafe) {
				printf("[+] zonename Error \n");
				}
			}
			zonename = value;
			checkSafe = safeacess(zonename);
			if(checkSafe){
				while(true)
				{
					uint8_t printzonename = kernel_read8(zonename);
					if(printzonename == 0x00)
					{
						goto jump;
					}
					if(printzonename != 0x00)
					{
						printf("printzonename => %c\n",printzonename);
						sprintf(a,"%c",printzonename);
						zonenamearray[cnt] = *a;
						zonename++;
						cnt++;
					}
				}

			}
			else if(!checkSafe) {
				printf("[+] zonename Error \n");
			}

			jump:
			printf("%s\n",zonenamearray);
			
			// zone element size
			uint64_t elementsize = findzone+0xf0;
			checkSafe = safeacess(elementsize);
			if(checkSafe){
				bool ok = kernel_read(elementsize, &value, sizeof(value));
				if (!ok) {
					return -1;
				}
			else if(!checkSafe) {
				printf("[+] elementsize Error \n");
				}
			}
			elementsize = value;

			printf("Zone => 0x%llx\n", findzone);
			//printf("zoneName => %s\n", zonenamearray);
			printf("Zone_metaData => 0x%llx\n",page_meta);
			printf("ElementSize => 0x%llx\n",elementsize);
		}
	}
	return true;
}

#include "boot_select.h"


BootSlot_t Boot_SelectSlot(bool slot_a_valid,
                           uint32_t slot_a_version,
                           bool slot_b_valid,
                           uint32_t slot_b_version)
{
   if ( slot_a_valid == false && slot_b_valid == false )
		 return BOOT_SLOT_NONE;
	 if ( slot_a_valid == true && slot_b_valid == false )
		 return BOOT_SLOT_A;
	 if ( slot_a_valid == false && slot_b_valid == true )
		 return BOOT_SLOT_B;
	 else{
		 if ( slot_a_version >= slot_b_version)
			 return BOOT_SLOT_A;
		 else
			 return BOOT_SLOT_B;
	 }
}

bool Boot_IsStateBootable(uint32_t state)
{
	if (state == (uint32_t)SLOT_STATE_CONFIRMED	||
			state == (uint32_t)SLOT_STATE_PENDING)
		return true;
	return false;
}

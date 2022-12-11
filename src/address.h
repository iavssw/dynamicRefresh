#define CH_A_Group 0
#define CH_A_Bus 0xff
#define CH_A_Device 0x14
#define CH_A_Func 0x0
#define CH_A_Err_Func 0x2

#define CH_B_Group 0
#define CH_B_Bus 0xff
#define CH_B_Device 0x14
#define CH_B_Func 0x1
#define CH_B_Err_Func 0x3

#define CH_C_Group 0
#define CH_C_Bus 0xff
#define CH_C_Device 0x15
#define CH_C_Func 0x0
#define CH_C_Err_Func 0x2

#define CH_D_Group 0
#define CH_D_Bus 0xff
#define CH_D_Device 0x15
#define CH_D_Func 0x1
#define CH_D_Err_Func 0x3

#define Temp_Off 0x150

#define tREFI_Off 0x214

#define Err_cnt_Off 0x104

#define step_tREFI_inc 0x40
#define step_tREFI_dec 0x100

#define base_tREFI 7280

// MAX tREFI 4.5xtREFI at 5'C, min tREFI 2tREFI at 85 'C
#define temp_slope base_tREFI * 2.5 / 80              // 182, tREFI/40
#define temp_offset base_tREFI * (4.5 + 2.5 / 80 * 5) // 30030, tREFIx4 + tREFI/8

#define num_channel 4

// use BW stuff
// #define BW_STUFF //comment to disable bandwith
#define average_loop_count 10

#define READ_ABS_MARGIN 50  // MB/s
#define WRITE_ABS_MARGIN 10 // MB/s
#define READ_REL_MARGIN 10  // X
#define WRITE_REL_MARGIN 5  // X
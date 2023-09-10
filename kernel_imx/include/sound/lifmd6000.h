#ifndef LIFMD6000
#define LIFMD6000

#ifdef CONFIG_LIFMD6000_RME_FW_WRITE
extern void lifmd6000_set_lock(int rock);
#else
static inline void lifmd6000_set_lock(int rock)
{
        return;
}
#endif
#endif

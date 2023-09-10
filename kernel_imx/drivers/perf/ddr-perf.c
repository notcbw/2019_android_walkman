/*
 * Copyright 2017 NXP
 * Copyright 2016 Freescale Semiconductor, Inc.
 * Copyright 2020 Sony Home Entertainment & Sound Products Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/perf_event.h>
#include <linux/slab.h>

#include <linux/busfreq-imx.h>
#include <linux/kthread.h>
#include <linux/ktime.h>

#define COUNTER_CNTL		0x0
#define COUNTER_READ		0x20

#define COUNTER_DPCR1		0x30

#define CNTL_OVER		0x1
#define CNTL_CLEAR		0x2
#define CNTL_EN			0x4
#define CNTL_EN_MASK		0xFFFFFFFB
#define CNTL_CLEAR_MASK		0xFFFFFFFD
#define CNTL_OVER_MASK		0xFFFFFFFE

#define CNTL_CSV_SHIFT		24
#define CNTL_CSV_MASK		(0xFF << CNTL_CSV_SHIFT)

#define EVENT_CYCLES_ID		0
#define EVENT_CYCLES_COUNTER	0
#define NUM_COUNTER		4
#define MAX_EVENT		3

#define to_ddr_pmu(p)		container_of(p, struct ddr_pmu, pmu)

#define DDR_PERF_DEV_NAME	"ddr_perf"

static DEFINE_IDA(ddr_ida);

PMU_EVENT_ATTR_STRING(cycles, ddr_perf_cycles, "event=0x00");
PMU_EVENT_ATTR_STRING(selfresh, ddr_perf_selfresh, "event=0x01");
PMU_EVENT_ATTR_STRING(read-access, ddr_perf_read_accesses, "event=0x04");
PMU_EVENT_ATTR_STRING(write-access, ddr_perf_write_accesses, "event=0x05");
PMU_EVENT_ATTR_STRING(read-queue-depth, ddr_perf_read_queue_depth,
		"event=0x08");
PMU_EVENT_ATTR_STRING(write-queue-depth, ddr_perf_write_queue_depth,
		"event=0x09");
PMU_EVENT_ATTR_STRING(lp-read-credit-cnt, ddr_perf_lp_read_credit_cnt,
		"event=0x10");
PMU_EVENT_ATTR_STRING(hp-read-credit-cnt, ddr_perf_hp_read_credit_cnt,
		"event=0x11");
PMU_EVENT_ATTR_STRING(write-credit-cnt, ddr_perf_write_credit_cnt,
		"event=0x12");
PMU_EVENT_ATTR_STRING(read-command, ddr_perf_read_command, "event=0x20");
PMU_EVENT_ATTR_STRING(write-command, ddr_perf_write_command, "event=0x21");
PMU_EVENT_ATTR_STRING(read-modify-write-command,
		ddr_perf_read_modify_write_command, "event=0x22");
PMU_EVENT_ATTR_STRING(hp-read, ddr_perf_hp_read, "event=0x23");
PMU_EVENT_ATTR_STRING(hp-req-nodcredit, ddr_perf_hp_req_nocredit, "event=0x24");
PMU_EVENT_ATTR_STRING(hp-xact-credit, ddr_perf_hp_xact_credit, "event=0x25");
PMU_EVENT_ATTR_STRING(lp-req-nocredit, ddr_perf_lp_req_nocredit, "event=0x26");
PMU_EVENT_ATTR_STRING(lp-xact-credit, ddr_perf_lp_xact_credit, "event=0x27");
PMU_EVENT_ATTR_STRING(wr-xact-credit, ddr_perf_wr_xact_credit, "event=0x29");
PMU_EVENT_ATTR_STRING(read-cycles, ddr_perf_read_cycles, "event=0x2a");
PMU_EVENT_ATTR_STRING(write-cycles, ddr_perf_write_cycles, "event=0x2b");
PMU_EVENT_ATTR_STRING(read-write-transition, ddr_perf_read_write_transition,
		"event=0x30");
PMU_EVENT_ATTR_STRING(precharge, ddr_perf_precharge, "event=0x31");
PMU_EVENT_ATTR_STRING(activate, ddr_perf_activate, "event=0x32");
PMU_EVENT_ATTR_STRING(load-mode, ddr_perf_load_mode, "event=0x33");
PMU_EVENT_ATTR_STRING(mwr, ddr_perf_mwr, "event=0x34");
PMU_EVENT_ATTR_STRING(read, ddr_perf_read, "event=0x35");
PMU_EVENT_ATTR_STRING(read-activate, ddr_perf_read_activate, "event=0x36");
PMU_EVENT_ATTR_STRING(refresh, ddr_perf_refresh, "event=0x37");
PMU_EVENT_ATTR_STRING(write, ddr_perf_write, "event=0x38");
PMU_EVENT_ATTR_STRING(raw-hazard, ddr_perf_raw_hazard, "event=0x39");

PMU_EVENT_ATTR_STRING(axid-read, ddr_perf_axid_read, "event=0x41");
PMU_EVENT_ATTR_STRING(axid-write, ddr_perf_axid_write, "event=0x42");

#define DDR_CAP_AXI_ID 0x1

struct fsl_ddr_devtype_data {
	unsigned int flags;
};

static const struct fsl_ddr_devtype_data imx8_data;
static const struct fsl_ddr_devtype_data imx8m_data = {
	.flags = DDR_CAP_AXI_ID,
};

static const struct of_device_id imx_ddr_pmu_dt_ids[] = {
	{ .compatible = "fsl,imx8-ddr-pmu", .data = (void*)&imx8_data},
	{ .compatible = "fsl,imx8m-ddr-pmu", .data = (void*)&imx8m_data},
	{ /* sentinel */ }
};

struct ddr_pmu {
	struct pmu pmu;
	void __iomem *base;
	cpumask_t cpu;
	struct	hlist_node node;
	struct	device *dev;
	struct perf_event *active_events[NUM_COUNTER];
	int total_events;
	bool cycles_active;
	struct fsl_ddr_devtype_data *devtype;
};

/* ddr monitor */
#define DDR_MON_COUNT0_CONFIG 0x0
#define DDR_MON_COUNT1_CONFIG 0x2a
#define DDR_MON_COUNT2_CONFIG 0x2b
static local64_t prev_count[3];
static int64_t ddr_count[3];
static struct task_struct *ddr_mon_kthread_tsk;
static struct kobject *ddr_mon_kobj;
static bool ddr_request_high;
static struct ddr_pmu *ddr_static_pmu;
static int64_t ddr_mon_count_high = 30000000;
static int64_t ddr_mon_count_low =  15000000;
static int ddr_mon_median_count_threth = 50;
static int ddr_mon_period_ms = 100;
static int ddr_mon_period_adjustment = 10;
static bool ddr_mon_enable;
static uint64_t ddr_request_count;

typedef struct ddr_mon_logging {
	int64_t  up_time;
	int64_t  cycles;
	int64_t  read_cycles;
	int64_t  write_cycles;
	uint64_t  count;
	bool      request;
	int median_count;
	int busfreq_now;
} DDR_PERF_LOGGING;
#define LOG_MAX_SIZE (100000)
#define LOG_SIZE_PER_LINE 100
#define LOG_SIZE_TOTAL 1500
#define LOG_READ_SIZE 15

static int is_log_trigger;
static int log_counter;
static int log_read_counter;
static int log_read_size = LOG_READ_SIZE;
static int log_size;
static DDR_PERF_LOGGING *logdata;

static ssize_t ddr_perf_cpumask_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev);

	return cpumap_print_to_pagebuf(true, buf, &pmu->cpu);
}

static struct device_attribute ddr_perf_cpumask_attr =
	__ATTR(cpumask, 0444, ddr_perf_cpumask_show, NULL);

static struct attribute *ddr_perf_cpumask_attrs[] = {
	&ddr_perf_cpumask_attr.attr,
	NULL,
};

static struct attribute_group ddr_perf_cpumask_attr_group = {
	.attrs = ddr_perf_cpumask_attrs,
};

static struct attribute *ddr_perf_events_attrs[] = {
	&ddr_perf_cycles.attr.attr,
	&ddr_perf_selfresh.attr.attr,
	&ddr_perf_read_accesses.attr.attr,
	&ddr_perf_write_accesses.attr.attr,
	&ddr_perf_read_queue_depth.attr.attr,
	&ddr_perf_write_queue_depth.attr.attr,
	&ddr_perf_lp_read_credit_cnt.attr.attr,
	&ddr_perf_hp_read_credit_cnt.attr.attr,
	&ddr_perf_write_credit_cnt.attr.attr,
	&ddr_perf_read_command.attr.attr,
	&ddr_perf_write_command.attr.attr,
	&ddr_perf_read_modify_write_command.attr.attr,
	&ddr_perf_hp_read.attr.attr,
	&ddr_perf_hp_req_nocredit.attr.attr,
	&ddr_perf_hp_xact_credit.attr.attr,
	&ddr_perf_lp_req_nocredit.attr.attr,
	&ddr_perf_lp_xact_credit.attr.attr,
	&ddr_perf_wr_xact_credit.attr.attr,
	&ddr_perf_read_cycles.attr.attr,
	&ddr_perf_write_cycles.attr.attr,
	&ddr_perf_read_write_transition.attr.attr,
	&ddr_perf_precharge.attr.attr,
	&ddr_perf_activate.attr.attr,
	&ddr_perf_load_mode.attr.attr,
	&ddr_perf_mwr.attr.attr,
	&ddr_perf_read.attr.attr,
	&ddr_perf_read_activate.attr.attr,
	&ddr_perf_refresh.attr.attr,
	&ddr_perf_write.attr.attr,
	&ddr_perf_raw_hazard.attr.attr,
	&ddr_perf_axid_read.attr.attr,
	&ddr_perf_axid_write.attr.attr,
	NULL,
};

static struct attribute_group ddr_perf_events_attr_group = {
	.name = "events",
	.attrs = ddr_perf_events_attrs,
};

PMU_FORMAT_ATTR(event, "config:0-63");
PMU_FORMAT_ATTR(axi_id, "config1:0-63");

static struct attribute *ddr_perf_format_attrs[] = {
	&format_attr_event.attr,
	&format_attr_axi_id.attr,
	NULL,
};

static struct attribute_group ddr_perf_format_attr_group = {
	.name = "format",
	.attrs = ddr_perf_format_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&ddr_perf_events_attr_group,
	&ddr_perf_format_attr_group,
	&ddr_perf_cpumask_attr_group,
	NULL,
};

static u32 ddr_perf_alloc_counter(struct ddr_pmu *pmu, int event)
{
	int i;

	/* Always map cycle event to counter 0 */
	if (event == EVENT_CYCLES_ID)
		return EVENT_CYCLES_COUNTER;

	for (i = 1; i < NUM_COUNTER; i++)
		if (pmu->active_events[i] == NULL)
			return i;

	return -ENOENT;
}

static u32 ddr_perf_free_counter(struct ddr_pmu *pmu, int counter)
{
	if (counter < 0 || counter >= NUM_COUNTER)
		return -ENOENT;

	pmu->active_events[counter] = NULL;

	return 0;
}

static u32 ddr_perf_read_counter(struct ddr_pmu *pmu, int counter)
{
	return readl(pmu->base + COUNTER_READ + counter * 4);
}

static int ddr_perf_event_init(struct perf_event *event)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK)
		return -EOPNOTSUPP;

	if (event->cpu < 0) {
		dev_warn(pmu->dev, "Can't provide per-task data!\n");
		return -EOPNOTSUPP;
	}

	if (event->attr.exclude_user        ||
	    event->attr.exclude_kernel      ||
	    event->attr.exclude_hv          ||
	    event->attr.exclude_idle        ||
	    event->attr.exclude_host        ||
	    event->attr.exclude_guest       ||
	    event->attr.sample_period)
		return -EINVAL;

	event->cpu = cpumask_first(&pmu->cpu);
	hwc->idx = -1;

	return 0;
}

static void ddr_mon_dram_enable(int counter, u8 config, bool enable)
{
	struct ddr_pmu *pmu = ddr_static_pmu;
	u8 reg = counter * 4 + COUNTER_CNTL;
	int val;

	if (enable) {
		/* Clear counter, then enable it. */
		writel(0, pmu->base + reg);
		val = CNTL_EN | CNTL_CLEAR;
		val |= (config << CNTL_CSV_SHIFT) & CNTL_CSV_MASK;
	} else {
		/* Disable counter */
		val = readl(pmu->base + reg) & CNTL_EN_MASK;
	}
	writel(val, pmu->base + reg);
}

static uint64_t ddr_mon_update(int counter)
{
	struct ddr_pmu *pmu = ddr_static_pmu;
	uint64_t delta, prev_raw_count, new_raw_count;
	int retry_count = 10;

	do {
		prev_raw_count = local64_read(&prev_count[counter]);
		new_raw_count = readl(pmu->base + COUNTER_READ + counter * 4);
		retry_count--;
	} while ((local64_cmpxchg(&prev_count[counter], prev_raw_count, new_raw_count) != prev_raw_count) && (retry_count != 0));
	if (retry_count == 0) {
		pr_err("%s:error counter value update\n", __func__);
		return 0;
	}

	if (new_raw_count > prev_raw_count) {
		delta = (new_raw_count - prev_raw_count) & 0xFFFFFFFF;
		delta = delta * ddr_mon_period_adjustment;
	} else {
		pr_debug("%s count:%d maybe overlap\n", __func__, counter);
		delta = 0;
	}

	return delta;
}

static bool dram_is_count_valid(int64_t *check_ddr_count, int bus_freq)
{
	const int64_t bus_high_count = 600000000;
	const int bus_high_threshold =  60000000;
	const int64_t bus_low_count =  100000000;
	const int bus_low_threshold =   30000000;
	int64_t ddr_cycle = check_ddr_count[0];
	uint64_t ddr_rw_total = check_ddr_count[1] + check_ddr_count[2];


	if (!check_ddr_count[0] || !check_ddr_count[1] || !check_ddr_count[2])
		return false;

	if (bus_freq == BUS_FREQ_HIGH) {
		if (ddr_cycle < (bus_high_count - bus_high_threshold) || ddr_cycle > (bus_high_count + bus_high_threshold))
			return false;
		if (bus_high_count < ddr_rw_total)
			return false;
	} else {
		if (ddr_cycle < (bus_low_count - bus_low_threshold) || ddr_cycle > (bus_low_count + bus_low_threshold))
			return false;
		if (bus_low_count < ddr_rw_total)
			return false;
	}
	return true;
}

static int ddr_mon_kthread(void *arg)
{
	int busfreq_now;
	int counter = 0;
	uint64_t ddr_count_total;
	bool is_valid_data;
	static int median_count;

	/* counter start */
	for (counter = 0; counter <= 2; counter++)
		local64_set(&prev_count[counter], 0);

	ddr_mon_dram_enable(0, DDR_MON_COUNT0_CONFIG, true);
	ddr_mon_dram_enable(1, DDR_MON_COUNT1_CONFIG, true);
	ddr_mon_dram_enable(2, DDR_MON_COUNT2_CONFIG, true);

	while (!kthread_should_stop()) {
		for (counter = 0; counter <= 2; counter++) {
			ddr_count[counter] = ddr_mon_update(counter);
			pr_debug("counter:%d:%llu\n", counter, ddr_count[counter]);
		}

		ddr_count_total = ddr_count[1] + ddr_count[2];
		busfreq_now = get_bus_freq_mode();
		is_valid_data = dram_is_count_valid(ddr_count, busfreq_now);
		if (is_valid_data) {
			if (!ddr_request_high && (ddr_mon_count_high < ddr_count_total)) {
				pr_debug("To High total:%lu high threshold:%lu\n", ddr_count_total, ddr_mon_count_high);
				request_bus_freq(BUS_FREQ_HIGH);
				ddr_request_count++;
				ddr_request_high = true;
			} else if (ddr_request_high && (ddr_count_total < ddr_mon_count_low)) {
				pr_debug("To Low total:%lu low threshold:%lu\n", ddr_count_total, ddr_mon_count_low);
				release_bus_freq(BUS_FREQ_HIGH);
				ddr_request_high = false;
			}

			if (ddr_request_high && (ddr_mon_count_low < ddr_count_total) && (ddr_count_total < ddr_mon_count_high)) {
				median_count++;
				if (ddr_mon_median_count_threth <= median_count) {
					pr_debug("To Low total:%lu median_count:%d\n", ddr_count_total, median_count);
					release_bus_freq(BUS_FREQ_HIGH);
					ddr_request_high = false;
					median_count = 0;
				}
			} else {
				median_count = 0;
			}

			if (is_log_trigger && (log_counter < log_size)) {
				logdata[log_counter].up_time = ktime_to_ms(ktime_get_boottime());
				logdata[log_counter].cycles = ddr_count[0];
				logdata[log_counter].read_cycles = ddr_count[1];
				logdata[log_counter].write_cycles = ddr_count[2];
				logdata[log_counter].count = ddr_request_count;
				logdata[log_counter].request = ddr_request_high;
				logdata[log_counter].median_count = median_count;
				logdata[log_counter].busfreq_now = busfreq_now;
				log_counter++;
			}
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(ddr_mon_period_ms));
	}

	//end kthread
	if (ddr_request_high) {
		release_bus_freq(BUS_FREQ_HIGH);
		ddr_request_high = false;
	}
	//counter stop
	ddr_mon_dram_enable(0, DDR_MON_COUNT0_CONFIG, false);
	ddr_mon_dram_enable(1, DDR_MON_COUNT1_CONFIG, false);
	ddr_mon_dram_enable(2, DDR_MON_COUNT2_CONFIG, false);

	return 0;
}

static void ddr_perf_event_update(struct perf_event *event)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u64 delta, prev_raw_count, new_raw_count;
	int counter = hwc->idx;

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		new_raw_count = ddr_perf_read_counter(pmu, counter);
	} while (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
			new_raw_count) != prev_raw_count);

	delta = (new_raw_count - prev_raw_count) & 0xFFFFFFFF;

	local64_add(delta, &event->count);
}

static void ddr_perf_event_enable(struct ddr_pmu *pmu, int config,
				  int counter, bool enable)
{
	u8 reg = counter * 4 + COUNTER_CNTL;
	int val;

	if (enable) {
		/* Clear counter, then enable it. */
		writel(0, pmu->base + reg);
		val = CNTL_EN | CNTL_CLEAR;
		val |= (config << CNTL_CSV_SHIFT) & CNTL_CSV_MASK;
	} else {
		/* Disable counter */
		val = readl(pmu->base + reg) & CNTL_EN_MASK;
	}

	writel(val, pmu->base + reg);

	if (config == EVENT_CYCLES_ID)
		pmu->cycles_active = enable;
}

static void ddr_perf_event_start(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	if (pmu->devtype->flags & DDR_CAP_AXI_ID) {
		if (event->attr.config == 0x41 ||
		    event->attr.config == 0x42) {
			int val = event->attr.config1;
			writel(val, pmu->base + COUNTER_DPCR1);
		}
	}

	local64_set(&hwc->prev_count, 0);

	ddr_perf_event_enable(pmu, event->attr.config, counter, true);
	/*
	 * If the cycles counter wasn't explicitly selected,
	 * we will enable it now.
	 */
	if (counter > 0 && !pmu->cycles_active)
		ddr_perf_event_enable(pmu, EVENT_CYCLES_ID,
				      EVENT_CYCLES_COUNTER, true);
}

static int ddr_perf_event_add(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter;
	int cfg = event->attr.config;

	counter = ddr_perf_alloc_counter(pmu, cfg);
	if (counter < 0) {
		dev_warn(pmu->dev, "There are not enough counters\n");
		return -EOPNOTSUPP;
	}

	pmu->active_events[counter] = event;
	pmu->total_events++;
	hwc->idx = counter;

	if (flags & PERF_EF_START)
		ddr_perf_event_start(event, flags);

	local64_set(&hwc->prev_count, ddr_perf_read_counter(pmu, counter));

	return 0;
}

static void ddr_perf_event_stop(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	ddr_perf_event_enable(pmu, event->attr.config, counter, false);
	ddr_perf_event_update(event);
}

static void ddr_perf_event_del(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	ddr_perf_event_stop(event, PERF_EF_UPDATE);

	ddr_perf_free_counter(pmu, counter);
	pmu->total_events--;
	hwc->idx = -1;

	/* If all events have stopped, stop the cycles counter as well */
	if ((pmu->total_events == 0) && pmu->cycles_active)
		ddr_perf_event_enable(pmu, EVENT_CYCLES_ID,
				      EVENT_CYCLES_COUNTER, false);
}

static int ddr_perf_init(struct ddr_pmu *pmu, void __iomem *base,
			 struct device *dev)
{
	*pmu = (struct ddr_pmu) {
		.pmu = (struct pmu) {
			.task_ctx_nr = perf_invalid_context,
			.attr_groups = attr_groups,
			.event_init  = ddr_perf_event_init,
			.add	     = ddr_perf_event_add,
			.del	     = ddr_perf_event_del,
			.start	     = ddr_perf_event_start,
			.stop	     = ddr_perf_event_stop,
			.read	     = ddr_perf_event_update,
		},
		.base = base,
		.dev = dev,
	};

	return ida_simple_get(&ddr_ida, 0, 0, GFP_KERNEL);
}

static irqreturn_t ddr_perf_irq_handler(int irq, void *p)
{
	int i;
	u8 reg;
	int val;
	int counter;
	struct ddr_pmu *pmu = (struct ddr_pmu *) p;
	struct perf_event *event;
	int config;

	/*
	 * The cycles counter has overflowed. Update all of the local counter
	 * values, then reset the cycles counter, so the others can continue
	 * counting.
	 */
	for (i = 0; i <= pmu->total_events; i++) {
		if (pmu->active_events[i] != NULL) {
			event = pmu->active_events[i];
			counter = event->hw.idx;
			reg = counter * 4 + COUNTER_CNTL;
			val = readl(pmu->base + reg);
			ddr_perf_event_update(event);
			if (val & CNTL_OVER) {
				/* Clear counter, then re-enable it. */
				ddr_perf_event_enable(pmu, event->attr.config,
						      counter, true);
				/* Update event again to reset prev_count */
				ddr_perf_event_update(event);
			}
		}
	}

	if (ddr_mon_enable) {
		for (i = 0; i <= 2; i++) {
			counter = i;
			reg = counter * 4 + COUNTER_CNTL;
			if (counter == 0)
				config = DDR_MON_COUNT0_CONFIG;
			else if (counter == 1)
				config = DDR_MON_COUNT1_CONFIG;
			else if (counter == 2)
				config = DDR_MON_COUNT2_CONFIG;
			val = readl(pmu->base + reg);
			ddr_mon_update(counter);
			if (val & CNTL_OVER) {
				/* Clear counter, then re-enable it. */
				ddr_mon_dram_enable(counter, config, true);
				/* Update event again to reset prev_count */
				ddr_mon_update(counter);
			}
		}
	}

	/*
	 * Reset the cycles counter regardless if it was explicitly
	 * enabled or not.
	 */
	ddr_perf_event_enable(pmu, EVENT_CYCLES_ID,
			      EVENT_CYCLES_COUNTER, true);

	return IRQ_HANDLED;
}

static ssize_t count_high_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%lld", &ddr_mon_count_high);
	return count;
}

static ssize_t count_high_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lld\n", ddr_mon_count_high);
}

static ssize_t count_low_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%lld", &ddr_mon_count_low);
	return count;
}

static ssize_t count_low_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lld\n", ddr_mon_count_low);
}

static ssize_t period_s_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &ddr_mon_period_ms);
	return count;
}

static ssize_t period_s_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ddr_mon_period_ms);
}

static ssize_t period_adjustment_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &ddr_mon_period_adjustment);
	return count;
}

static ssize_t period_adjustment_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ddr_mon_period_adjustment);
}

static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &ddr_mon_enable);
	if (ddr_mon_enable && !ddr_mon_kthread_tsk) {
		ddr_mon_kthread_tsk = kthread_run(ddr_mon_kthread, NULL, "ddr_mon_kthread");
	} else if (!ddr_mon_enable && ddr_mon_kthread_tsk) {
		kthread_stop(ddr_mon_kthread_tsk);
		ddr_mon_kthread_tsk = NULL;
	}
	return count;
}

static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ddr_mon_enable);
}

static ssize_t request_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ddr_request_high);
}

static ssize_t request_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%llu\n", ddr_request_count);
}

static ssize_t status_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "cycles:%lld\nread-cycles:%lld\nwrite-cycles:%lld\nTotal:%lld\ncount_high:%lld\ncount_low:%lld\ncount:%llu\nlog_counter:%d,log_read:%d,request:%d,enable:%d\n",
			ddr_count[0], ddr_count[1], ddr_count[2], ddr_count[1] + ddr_count[2], ddr_mon_count_high, ddr_mon_count_low, ddr_request_count, log_counter, log_read_counter, ddr_request_high, ddr_mon_enable);
}

static ssize_t log_trigger_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int tmp;

	sscanf(buf, "%d", &tmp);

	if (!logdata && tmp) {
		logdata = kzalloc(sizeof(DDR_PERF_LOGGING) * LOG_MAX_SIZE, GFP_KERNEL);
		log_size = LOG_MAX_SIZE;
		if (!logdata) {
			pr_err("%s can't malloc memory!!\n", __func__);
			log_size = 0;
		}
	}
	is_log_trigger = tmp;

	return count;
}

static ssize_t log_trigger_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", is_log_trigger);
}

static ssize_t data_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int i;
	char tmp_buf[LOG_SIZE_PER_LINE];
	char total_buf[LOG_SIZE_TOTAL];

	if (log_read_counter == log_counter)
		return 0;

	for (i = log_read_counter; i < (log_read_counter + log_read_size); i++) {
		snprintf(tmp_buf, sizeof(tmp_buf), "%lld,%lld,%lld,%lld,%llu,%d,%d,%d\n",
		 logdata[i].up_time, logdata[i].cycles, logdata[i].read_cycles, logdata[i].write_cycles, logdata[i].count, logdata[i].request, logdata[i].median_count, logdata[i].busfreq_now);
		strncat(total_buf, tmp_buf, sizeof(tmp_buf));
	}
	if (log_read_counter + log_read_size <= log_counter)
		log_read_counter += log_read_size;
	else
		log_read_counter = log_counter;

	return sprintf(buf, "%s", total_buf);
}

static struct kobj_attribute count_high_attribute = __ATTR(count_high, 0644, count_high_show, count_high_store);
static struct kobj_attribute count_low_attribute = __ATTR(count_low, 0644, count_low_show, count_low_store);
static struct kobj_attribute period_s_attribute = __ATTR(period_ms, 0644, period_s_show, period_s_store);
static struct kobj_attribute period_adjustment_attribute = __ATTR(period_adjustment, 0644, period_adjustment_show, period_adjustment_store);
static struct kobj_attribute enable_attribute = __ATTR(enable, 0644, enable_show, enable_store);
static struct kobj_attribute request_attribute = __ATTR(request, 0444, request_show, NULL);
static struct kobj_attribute request_count_attribute = __ATTR(request_count, 0444, request_count_show, NULL);
static struct kobj_attribute status_attribute = __ATTR(status, 0444, status_show, NULL);
static struct kobj_attribute log_trigger_attribute = __ATTR(log_trigger, 0644, log_trigger_show, log_trigger_store);
static struct kobj_attribute data_attribute = __ATTR(data, 0444, data_show, NULL);

static struct attribute *attrs[] = {
	&count_high_attribute.attr,
	&count_low_attribute.attr,
	&period_s_attribute.attr,
	&period_adjustment_attribute.attr,
	&enable_attribute.attr,
	&request_attribute.attr,
	&request_count_attribute.attr,
	&status_attribute.attr,
	&log_trigger_attribute.attr,
	&data_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static int ddr_perf_probe(struct platform_device *pdev)
{
	struct ddr_pmu *pmu;
	struct device_node *np;
	void __iomem *base;
	struct resource *iomem;
	char *name;
	int num;
	int ret;
	u32 irq;
	const struct of_device_id *of_id =
		of_match_device(imx_ddr_pmu_dt_ids, &pdev->dev);

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, iomem);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		return ret;
	}

	np = pdev->dev.of_node;

	pmu = kzalloc(sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	num = ddr_perf_init(pmu, base, &pdev->dev);
	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "ddr%d", num);

	pmu->devtype = (struct fsl_ddr_devtype_data *)of_id->data;

	cpumask_set_cpu(smp_processor_id(), &pmu->cpu);
	ret = perf_pmu_register(&(pmu->pmu), name, -1);
	if (ret)
		goto ddr_perf_err;

	/* Request irq */
	irq = of_irq_get(np, 0);
	if (irq < 0) {
		pr_err("Failed to get irq: %d", irq);
		goto ddr_perf_err;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq,
					ddr_perf_irq_handler, NULL,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					DDR_PERF_DEV_NAME,
					pmu);
	if (ret < 0) {
		pr_err("Request irq failed: %d", ret);
		goto ddr_perf_irq_err;
	}

	ddr_static_pmu = pmu;
	ddr_mon_kobj = kobject_create_and_add("ddr_perf", kernel_kobj);
	if (!ddr_mon_kobj)
		return -ENOMEM;
	ret = sysfs_create_group(ddr_mon_kobj, &attr_group);
	if (ret)
		kobject_put(ddr_mon_kobj);

	ddr_mon_kthread_tsk = kthread_run(ddr_mon_kthread, NULL, "ddr_mon_kthread");
	ddr_mon_enable = true;

	return 0;

ddr_perf_irq_err:
	perf_pmu_unregister(&(pmu->pmu));
ddr_perf_err:
	pr_warn("i.MX8 DDR Perf PMU failed (%d), disabled\n", ret);
	kfree(pmu);
	return ret;
}


static int ddr_perf_remove(struct platform_device *pdev)
{
	struct ddr_pmu *pmu = platform_get_drvdata(pdev);

	perf_pmu_unregister(&pmu->pmu);
	kfree(pmu);

	return 0;
}

static int ddr_perf_suspend(struct device *dev)
{
	if (ddr_mon_kthread_tsk != NULL) {
		kthread_stop(ddr_mon_kthread_tsk);
		ddr_mon_kthread_tsk = NULL;
	}
	return 0;
}

static int ddr_perf_resume(struct device *dev)
{
	if (ddr_mon_enable && !ddr_mon_kthread_tsk)
		ddr_mon_kthread_tsk = kthread_run(ddr_mon_kthread, NULL, "ddr_mon_kthread");
	return 0;
}

static const struct dev_pm_ops ddr_perf_dev_pmops = {
	.suspend = ddr_perf_suspend,
	.resume  = ddr_perf_resume,
};


static struct platform_driver imx_ddr_pmu_driver = {
	.driver         = {
		.name   = "imx-ddr-pmu",
		.of_match_table = imx_ddr_pmu_dt_ids,
		.pm    = &ddr_perf_dev_pmops,
	},
	.probe          = ddr_perf_probe,
	.remove         = ddr_perf_remove,
};

static int __init imx_ddr_pmu_init(void)
{
	return platform_driver_register(&imx_ddr_pmu_driver);
}

module_init(imx_ddr_pmu_init);


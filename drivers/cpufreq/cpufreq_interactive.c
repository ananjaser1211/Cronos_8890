/*
 * drivers/cpufreq/cpufreq_interactive.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Mike Chan (mike@android.com)
 *
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/tick.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/pm_qos.h>
#include <linux/cpu_pm.h>

#include <soc/samsung/pmu_func.h>

#ifdef CONFIG_CPU_THERMAL_IPA
#include "cpu_load_metric.h"
#include <linux/ipa.h>
#endif

#define CREATE_TRACE_POINTS
#include <trace/events/cpufreq_interactive.h>

#define CONFIG_DYNAMIC_MODE_SUPPORT
//#define CONFIG_DYNAMIC_MODE_SUPPORT_DEBUG

struct cpufreq_interactive_cpuinfo {
	struct timer_list cpu_timer;
	struct timer_list cpu_slack_timer;
	spinlock_t load_lock; /* protects the next 4 fields */
	u64 time_in_idle;
	u64 time_in_idle_timestamp;
	u64 cputime_speedadj;
	u64 cputime_speedadj_timestamp;
	struct cpufreq_policy *policy;
	struct cpufreq_frequency_table *freq_table;
	spinlock_t target_freq_lock; /*protects target freq */
	unsigned int target_freq;
	unsigned int floor_freq;
	u64 pol_floor_val_time; /* policy floor_validate_time */
	u64 loc_floor_val_time; /* per-cpu floor_validate_time */
	u64 pol_hispeed_val_time; /* policy hispeed_validate_time */
	u64 loc_hispeed_val_time; /* per-cpu hispeed_validate_time */
	struct rw_semaphore enable_sem;
	int governor_enabled;
	u64 delta_idle;
	struct pmu_count_value pdata;
};

static DEFINE_PER_CPU(struct cpufreq_interactive_cpuinfo, cpuinfo);

#define TASK_NAME_LEN 15
struct task_struct *speedchange_task;
static cpumask_t speedchange_cpumask;
static spinlock_t speedchange_cpumask_lock;
static struct mutex gov_lock;

/* Target load.  Lower values result in higher CPU speeds. */
#define DEFAULT_TARGET_LOAD 90
static unsigned int default_target_loads[] = {DEFAULT_TARGET_LOAD};

#define DEFAULT_TIMER_RATE (20 * USEC_PER_MSEC)
#define DEFAULT_ABOVE_HISPEED_DELAY DEFAULT_TIMER_RATE
static unsigned int default_above_hispeed_delay[] = {
	DEFAULT_ABOVE_HISPEED_DELAY };

#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
#define PERF_MODE 2
#define SLOW_MODE 1
#define NORMAL_MODE 0
static bool hmp_boost;
#endif

struct cpufreq_interactive_tunables {
	int usage_count;
	/* Hi speed to bump to from lo speed when load burst (default max) */
	unsigned int hispeed_freq;
	/* Go to hi speed when CPU load at or above this value. */
#define DEFAULT_GO_HISPEED_LOAD 97
	unsigned long go_hispeed_load;
	/* Target load. Lower values result in higher CPU speeds. */
	spinlock_t target_loads_lock;
	unsigned int *target_loads;
	int ntarget_loads;
	/*
	 * The minimum amount of time to spend at a frequency before we can ramp
	 * down.
	 */
#define DEFAULT_MIN_SAMPLE_TIME (50 * USEC_PER_MSEC)
	unsigned long min_sample_time;
	/*
	 * The sample rate of the timer used to increase frequency
	 */
	unsigned long timer_rate;
	/*
	 * Wait this long before raising speed above hispeed, by default a
	 * single timer interval.
	 */
	spinlock_t above_hispeed_delay_lock;
	unsigned int *above_hispeed_delay;
	int nabove_hispeed_delay;
	/* Non-zero means indefinite speed boost active */
	int boost_val;
	/* Duration of a boot pulse in usecs */
	int boostpulse_duration_val;
	/* End time of boost pulse in ktime converted to usecs */
	u64 boostpulse_endtime;
	bool boosted;
	/*
	 * Max additional time to wait in idle, beyond timer_rate, at speeds
	 * above minimum before wakeup to reduce speed, or -1 if unnecessary.
	 */
#define DEFAULT_TIMER_SLACK (4 * DEFAULT_TIMER_RATE)
	int timer_slack_val;
	bool io_is_busy;

	/* handle for get cpufreq_policy */
	unsigned int *policy;
#ifdef CONFIG_EXYNOS_WD_DVFS
#define DEFAULT_WD_BOUNDARY 1600
	u32 wd_boundary;
#endif

#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
#define MAX_PARAM_SET 3 //* DEFAULT | SLOW | PERF *//
	spinlock_t mode_lock;
	spinlock_t param_index_lock;

	unsigned int mode;
	unsigned int old_mode;
	unsigned int param_index;
	unsigned int cur_param_index;

	unsigned int *target_loads_set[MAX_PARAM_SET];
	int ntarget_loads_set[MAX_PARAM_SET];

	unsigned int hispeed_freq_set[MAX_PARAM_SET];

	unsigned long go_hispeed_load_set[MAX_PARAM_SET];

	unsigned int *above_hispeed_delay_set[MAX_PARAM_SET];
	int nabove_hispeed_delay_set[MAX_PARAM_SET];
#endif
};

/* For cases where we have single governor instance for system */
static struct cpufreq_interactive_tunables *common_tunables;
static struct cpufreq_interactive_tunables *tuned_parameters[NR_CPUS] = {NULL, };

static struct attribute_group *get_sysfs_attr(void);

static void cpufreq_interactive_timer_resched(
	struct cpufreq_interactive_cpuinfo *pcpu)
{
	struct cpufreq_interactive_tunables *tunables =
		pcpu->policy->governor_data;
	unsigned long expires;
	unsigned long flags;

	spin_lock_irqsave(&pcpu->load_lock, flags);
	pcpu->time_in_idle =
		get_cpu_idle_time(smp_processor_id(),
				  &pcpu->time_in_idle_timestamp,
				  tunables->io_is_busy);
	pcpu->cputime_speedadj = 0;
	pcpu->delta_idle = 0;
	pcpu->cputime_speedadj_timestamp = pcpu->time_in_idle_timestamp;
	expires = jiffies + usecs_to_jiffies(tunables->timer_rate);
	mod_timer_pinned(&pcpu->cpu_timer, expires);

	if (tunables->timer_slack_val >= 0 &&
	    pcpu->target_freq > pcpu->policy->min) {
		expires += usecs_to_jiffies(tunables->timer_slack_val);
		mod_timer_pinned(&pcpu->cpu_slack_timer, expires);
	}

	spin_unlock_irqrestore(&pcpu->load_lock, flags);
}

/* The caller shall take enable_sem write semaphore to avoid any timer race.
 * The cpu_timer and cpu_slack_timer must be deactivated when calling this
 * function.
 */
static void cpufreq_interactive_timer_start(
	struct cpufreq_interactive_tunables *tunables, int cpu)
{
	struct cpufreq_interactive_cpuinfo *pcpu = &per_cpu(cpuinfo, cpu);
	unsigned long expires = jiffies +
		usecs_to_jiffies(tunables->timer_rate);
	unsigned long flags;

	pcpu->cpu_timer.expires = expires;
	add_timer_on(&pcpu->cpu_timer, cpu);
	if (tunables->timer_slack_val >= 0 &&
	    pcpu->target_freq > pcpu->policy->min) {
		expires += usecs_to_jiffies(tunables->timer_slack_val);
		pcpu->cpu_slack_timer.expires = expires;
		add_timer_on(&pcpu->cpu_slack_timer, cpu);
	}

	spin_lock_irqsave(&pcpu->load_lock, flags);
	pcpu->time_in_idle =
		get_cpu_idle_time(cpu, &pcpu->time_in_idle_timestamp,
				  tunables->io_is_busy);
	pcpu->cputime_speedadj = 0;
	pcpu->delta_idle = 0;
	pcpu->cputime_speedadj_timestamp = pcpu->time_in_idle_timestamp;
	spin_unlock_irqrestore(&pcpu->load_lock, flags);
}

static unsigned int freq_to_above_hispeed_delay(
	struct cpufreq_interactive_tunables *tunables,
	unsigned int freq)
{
	int i;
	unsigned int ret;
	unsigned long flags;

	spin_lock_irqsave(&tunables->above_hispeed_delay_lock, flags);

	for (i = 0; i < tunables->nabove_hispeed_delay - 1 &&
			freq >= tunables->above_hispeed_delay[i+1]; i += 2)
		;

	ret = tunables->above_hispeed_delay[i];
	spin_unlock_irqrestore(&tunables->above_hispeed_delay_lock, flags);
	return ret;
}

static unsigned int freq_to_targetload(
	struct cpufreq_interactive_tunables *tunables, unsigned int freq)
{
	int i;
	unsigned int ret;
	unsigned long flags;

	spin_lock_irqsave(&tunables->target_loads_lock, flags);

	for (i = 0; i < tunables->ntarget_loads - 1 &&
		    freq >= tunables->target_loads[i+1]; i += 2)
		;

	ret = tunables->target_loads[i];
	spin_unlock_irqrestore(&tunables->target_loads_lock, flags);
	return ret;
}

/*
 * If increasing frequencies never map to a lower target load then
 * choose_freq() will find the minimum frequency that does not exceed its
 * target load given the current load.
 */
static unsigned int choose_freq(struct cpufreq_interactive_cpuinfo *pcpu,
		unsigned int loadadjfreq)
{
	unsigned int freq = pcpu->policy->cur;
	unsigned int prevfreq, freqmin, freqmax;
	unsigned int tl;
	int index;

	freqmin = 0;
	freqmax = UINT_MAX;

	do {
		prevfreq = freq;
		tl = freq_to_targetload(pcpu->policy->governor_data, freq);

		/*
		 * Find the lowest frequency where the computed load is less
		 * than or equal to the target load.
		 */

		if (cpufreq_frequency_table_target(
			    pcpu->policy, pcpu->freq_table, loadadjfreq / tl,
			    CPUFREQ_RELATION_L, &index))
			break;
		freq = pcpu->freq_table[index].frequency;

		if (freq > prevfreq) {
			/* The previous frequency is too low. */
			freqmin = prevfreq;

			if (freq >= freqmax) {
				/*
				 * Find the highest frequency that is less
				 * than freqmax.
				 */
				if (cpufreq_frequency_table_target(
					    pcpu->policy, pcpu->freq_table,
					    freqmax - 1, CPUFREQ_RELATION_H,
					    &index))
					break;
				freq = pcpu->freq_table[index].frequency;

				if (freq == freqmin) {
					/*
					 * The first frequency below freqmax
					 * has already been found to be too
					 * low.  freqmax is the lowest speed
					 * we found that is fast enough.
					 */
					freq = freqmax;
					break;
				}
			}
		} else if (freq < prevfreq) {
			/* The previous frequency is high enough. */
			freqmax = prevfreq;

			if (freq <= freqmin) {
				/*
				 * Find the lowest frequency that is higher
				 * than freqmin.
				 */
				if (cpufreq_frequency_table_target(
					    pcpu->policy, pcpu->freq_table,
					    freqmin + 1, CPUFREQ_RELATION_L,
					    &index))
					break;
				freq = pcpu->freq_table[index].frequency;

				/*
				 * If freqmax is the first frequency above
				 * freqmin then we have already found that
				 * this speed is fast enough.
				 */
				if (freq == freqmax)
					break;
			}
		}

		/* If same frequency chosen as previous then done. */
	} while (freq != prevfreq);

	return freq;
}

static u64 update_load(int cpu)
{
	struct cpufreq_interactive_cpuinfo *pcpu = &per_cpu(cpuinfo, cpu);
	struct cpufreq_interactive_tunables *tunables =
		pcpu->policy->governor_data;
	u64 now;
	u64 now_idle;
	unsigned int delta_idle;
	unsigned int delta_time;
	u64 active_time;

	now_idle = get_cpu_idle_time(cpu, &now, tunables->io_is_busy);
	delta_idle = (unsigned int)(now_idle - pcpu->time_in_idle);
	delta_time = (unsigned int)(now - pcpu->time_in_idle_timestamp);

	if (delta_time <= delta_idle)
		active_time = 0;
	else
		active_time = delta_time - delta_idle;

	pcpu->cputime_speedadj += active_time * pcpu->policy->cur;
	pcpu->delta_idle += delta_idle;

#ifdef CONFIG_CPU_THERMAL_IPA
	update_cpu_metric(cpu, now, delta_idle, delta_time, pcpu->policy);
#endif

	pcpu->time_in_idle = now_idle;
	pcpu->time_in_idle_timestamp = now;
	return now;
}

#ifdef CONFIG_DYNAMIC_MODE_SUPPORT

static void set_new_param_set(unsigned int index,
			struct cpufreq_interactive_tunables * tunables)
{
	unsigned long flags;
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT_DEBUG
	int i;
	int j;
#endif
	tunables->hispeed_freq = tunables->hispeed_freq_set[index];
	tunables->go_hispeed_load = tunables->go_hispeed_load_set[index];

	spin_lock_irqsave(&tunables->target_loads_lock, flags);
	tunables->target_loads = tunables->target_loads_set[index];
	tunables->ntarget_loads = tunables->ntarget_loads_set[index];
	spin_unlock_irqrestore(&tunables->target_loads_lock, flags);

	spin_lock_irqsave(&tunables->above_hispeed_delay_lock, flags);
	tunables->above_hispeed_delay =
		tunables->above_hispeed_delay_set[index];
	tunables->nabove_hispeed_delay =
		tunables->nabove_hispeed_delay_set[index];
	spin_unlock_irqrestore(&tunables->above_hispeed_delay_lock, flags);
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT_DEBUG
	printk("change to idx:%u\n", index);
	for(i=0; i<MAX_PARAM_SET;i++){
		printk("target_loads[%d]=", i);
		for(j=0; j < tunables->ntarget_loads_set[i]; j++) printk("%u:", tunables->target_loads_set[i][j]);
		printk("\n");
		printk("above_hispeed_delay[%d]=", i);
		for(j=0; j < tunables->nabove_hispeed_delay_set[i]; j++) printk("%u:", tunables->above_hispeed_delay_set[i][j]);
		printk("\n");
		printk("hispeed_freq[%d]:%u, go_hispeed_load[%d]:%lu\n", i, tunables->hispeed_freq_set[i], i, tunables->go_hispeed_load_set[i]);
	}
#endif
	tunables->cur_param_index = index;
}
static void enter_mode(struct cpufreq_interactive_tunables * tunables)
{
	pr_info("Governor: enter mode 0x%x\n", tunables->mode);
	set_new_param_set(tunables->mode, tunables);

	if(!hmp_boost && (tunables->mode & PERF_MODE)) {
		//pr_debug("%s mp boost on", __func__);
		//(void)set_hmp_boost(1);
		hmp_boost = true;
	}else if(hmp_boost && (tunables->mode & SLOW_MODE)){
		//pr_debug("%s mp boost off", __func__);
		//(void)set_hmp_boost(0);
		hmp_boost = false;
	}
}

static void exit_mode(struct cpufreq_interactive_tunables * tunables)
{
	pr_info("Governor: exit mode 0x%x\n", tunables->mode);
	set_new_param_set(0, tunables);

	if(hmp_boost) {
		//pr_debug("%s mp boost off", __func__);
		//(void)set_hmp_boost(0);
		hmp_boost = false;
	}
}
#endif

static void cpufreq_interactive_timer(unsigned long data)
{
#ifdef CONFIG_EXYNOS_WD_DVFS
	u64 cpki;
	u64 core = 1;
	u64 mem = 0;
#endif
	u64 now;
	unsigned int delta_time;
	u64 cputime_speedadj;
	int cpu_load;
	struct cpufreq_interactive_cpuinfo *pcpu =
		&per_cpu(cpuinfo, data);
	struct cpufreq_interactive_tunables *tunables =
		pcpu->policy->governor_data;
	unsigned int new_freq;
	unsigned int loadadjfreq;
	unsigned int index;
	unsigned long flags;
	u64 max_fvtime;

	if (!down_read_trylock(&pcpu->enable_sem))
		return;
	if (!pcpu->governor_enabled)
		goto exit;

#ifdef CONFIG_EXYNOS_WD_DVFS
	stop_counter_cpu();
	read_pmu_one(&pcpu->pdata);
	cpki = ((u64)pcpu->pdata.pmnc0 << 10) / (pcpu->pdata.pmnc1 + 1) + 1;
	reset_pmu_one(&pcpu->pdata);
	start_counter_cpu();
	if (cpki < tunables->wd_boundary) {
		core = cpki;
	} else {
		core = tunables->wd_boundary;
		mem = cpki - tunables->wd_boundary;
	}
#endif

	spin_lock_irqsave(&pcpu->load_lock, flags);
	now = update_load(data);
	delta_time = (unsigned int)(now - pcpu->cputime_speedadj_timestamp);
	cputime_speedadj = pcpu->cputime_speedadj;
	spin_unlock_irqrestore(&pcpu->load_lock, flags);

	if (WARN_ON_ONCE(!delta_time))
		goto rearm;
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	spin_lock_irqsave(&tunables->mode_lock, flags);
	if(tunables->old_mode != tunables->mode){
		if(tunables->mode & PERF_MODE || tunables->mode & SLOW_MODE)
			enter_mode(tunables);
		else
			exit_mode(tunables);
	}
	tunables->old_mode = tunables->mode;
	spin_unlock_irqrestore(&tunables->mode_lock, flags);
#endif

	spin_lock_irqsave(&pcpu->target_freq_lock, flags);
#ifdef CONFIG_EXYNOS_WD_DVFS
	do_div(cputime_speedadj, delta_time + mem * pcpu->delta_idle / core);
#else
	do_div(cputime_speedadj, delta_time);
#endif
	loadadjfreq = (unsigned int)cputime_speedadj * 100;
	cpu_load = loadadjfreq / pcpu->policy->cur;
	tunables->boosted = tunables->boost_val || now < tunables->boostpulse_endtime;

	if (cpu_load >= tunables->go_hispeed_load || tunables->boosted) {
		if (pcpu->policy->cur < tunables->hispeed_freq) {
			new_freq = tunables->hispeed_freq;
		} else {
			new_freq = choose_freq(pcpu, loadadjfreq);

			if (new_freq < tunables->hispeed_freq)
				new_freq = tunables->hispeed_freq;
		}
	} else {
		new_freq = choose_freq(pcpu, loadadjfreq);
		if (new_freq > tunables->hispeed_freq &&
				pcpu->policy->cur < tunables->hispeed_freq)
			new_freq = tunables->hispeed_freq;
	}

	if (cpufreq_frequency_table_target(pcpu->policy, pcpu->freq_table,
					   new_freq, CPUFREQ_RELATION_L,
					   &index)) {
		spin_unlock_irqrestore(&pcpu->target_freq_lock, flags);
		goto rearm;
	}

	new_freq = pcpu->freq_table[index].frequency;

	if (pcpu->policy->cur >= tunables->hispeed_freq &&
	    new_freq > pcpu->policy->cur &&
	    now - pcpu->pol_hispeed_val_time <
	    freq_to_above_hispeed_delay(tunables, pcpu->policy->cur)) {
		trace_cpufreq_interactive_notyet(
			data, cpu_load, pcpu->target_freq,
			pcpu->policy->cur, new_freq);
		spin_unlock_irqrestore(&pcpu->target_freq_lock, flags);
		goto target_update;
	}

	pcpu->loc_hispeed_val_time = now;

	/*
	 * Do not scale below floor_freq unless we have been at or above the
	 * floor frequency for the minimum sample time since last validated.
	 */
	max_fvtime = max(pcpu->pol_floor_val_time, pcpu->loc_floor_val_time);
	if (new_freq < pcpu->floor_freq &&
	    pcpu->target_freq >= pcpu->policy->cur) {
		if (now - max_fvtime < tunables->min_sample_time) {
			trace_cpufreq_interactive_notyet(
				data, cpu_load, pcpu->target_freq,
				pcpu->policy->cur, new_freq);
			spin_unlock_irqrestore(&pcpu->target_freq_lock, flags);
			goto rearm;
		}
	}

	/*
	 * Update the timestamp for checking whether speed has been held at
	 * or above the selected frequency for a minimum of min_sample_time,
	 * if not boosted to hispeed_freq.  If boosted to hispeed_freq then we
	 * allow the speed to drop as soon as the boostpulse duration expires
	 * (or the indefinite boost is turned off).
	 */

	if (!tunables->boosted || new_freq > tunables->hispeed_freq) {
		pcpu->floor_freq = new_freq;
		if (pcpu->target_freq >= pcpu->policy->cur ||
		    new_freq >= pcpu->policy->cur)
			pcpu->loc_floor_val_time = now;
	}

	if (pcpu->target_freq == new_freq &&
			pcpu->target_freq <= pcpu->policy->cur) {
		trace_cpufreq_interactive_already(
			data, cpu_load, pcpu->target_freq,
			pcpu->policy->cur, new_freq);
		spin_unlock_irqrestore(&pcpu->target_freq_lock, flags);
		goto rearm;
	}

	trace_cpufreq_interactive_target(data, cpu_load, pcpu->target_freq,
					 pcpu->policy->cur, new_freq);

	pcpu->target_freq = new_freq;
	spin_unlock_irqrestore(&pcpu->target_freq_lock, flags);
	spin_lock_irqsave(&speedchange_cpumask_lock, flags);
	cpumask_set_cpu(data, &speedchange_cpumask);
	spin_unlock_irqrestore(&speedchange_cpumask_lock, flags);
	wake_up_process(speedchange_task);

	goto rearm;

target_update:
	pcpu->target_freq = pcpu->policy->cur;

rearm:
	if (!timer_pending(&pcpu->cpu_timer))
		cpufreq_interactive_timer_resched(pcpu);

exit:
	up_read(&pcpu->enable_sem);
	return;
}

static void cpufreq_interactive_idle_end(void)
{
	struct cpufreq_interactive_cpuinfo *pcpu =
		&per_cpu(cpuinfo, smp_processor_id());

	if (!down_read_trylock(&pcpu->enable_sem))
		return;
	if (!pcpu->governor_enabled) {
		up_read(&pcpu->enable_sem);
		return;
	}

	/* Arm the timer for 1-2 ticks later if not already. */
	if (!timer_pending(&pcpu->cpu_timer)) {
		cpufreq_interactive_timer_resched(pcpu);
	} else if (time_after_eq(jiffies, pcpu->cpu_timer.expires)) {
		del_timer(&pcpu->cpu_timer);
		del_timer(&pcpu->cpu_slack_timer);
		cpufreq_interactive_timer(smp_processor_id());
	}

	up_read(&pcpu->enable_sem);
}

static void cpufreq_interactive_get_policy_info(struct cpufreq_policy *policy,
						unsigned int *pmax_freq,
						u64 *phvt, u64 *pfvt)
{
	struct cpufreq_interactive_cpuinfo *pcpu;
	unsigned int max_freq = 0;
	u64 hvt = ~0ULL, fvt = 0;
	unsigned int i;

	for_each_cpu(i, policy->cpus) {
		pcpu = &per_cpu(cpuinfo, i);

		fvt = max(fvt, pcpu->loc_floor_val_time);
		if (pcpu->target_freq > max_freq) {
			max_freq = pcpu->target_freq;
			hvt = pcpu->loc_hispeed_val_time;
		} else if (pcpu->target_freq == max_freq) {
			hvt = min(hvt, pcpu->loc_hispeed_val_time);
		}
	}

	*pmax_freq = max_freq;
	*phvt = hvt;
	*pfvt = fvt;
}

static void cpufreq_interactive_adjust_cpu(unsigned int cpu,
					   struct cpufreq_policy *policy)
{
	struct cpufreq_interactive_cpuinfo *pcpu;
	u64 hvt, fvt;
	unsigned int max_freq;
	int i;

	cpufreq_interactive_get_policy_info(policy, &max_freq, &hvt, &fvt);

	for_each_cpu(i, policy->cpus) {
		pcpu = &per_cpu(cpuinfo, i);
		pcpu->pol_floor_val_time = fvt;
	}

	if (max_freq != policy->cur) {
		__cpufreq_driver_target(policy, max_freq, CPUFREQ_RELATION_H);
		for_each_cpu(i, policy->cpus) {
			pcpu = &per_cpu(cpuinfo, i);
			pcpu->pol_hispeed_val_time = hvt;
		}
	}

#if defined(CONFIG_CPU_THERMAL_IPA)
			ipa_cpufreq_requested(pcpu->policy, max_freq);
#endif

	trace_cpufreq_interactive_setspeed(cpu, max_freq, policy->cur);
}

static int cpufreq_interactive_speedchange_task(void *data)
{
	unsigned int cpu;
	cpumask_t tmp_mask;
	unsigned long flags;
	struct cpufreq_interactive_cpuinfo *pcpu;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&speedchange_cpumask_lock, flags);

		if (cpumask_empty(&speedchange_cpumask)) {
			spin_unlock_irqrestore(&speedchange_cpumask_lock,
					       flags);
			schedule();

			if (kthread_should_stop())
				break;

			spin_lock_irqsave(&speedchange_cpumask_lock, flags);
		}

		set_current_state(TASK_RUNNING);
		tmp_mask = speedchange_cpumask;
		cpumask_clear(&speedchange_cpumask);

		spin_unlock_irqrestore(&speedchange_cpumask_lock, flags);

		for_each_cpu(cpu, &tmp_mask) {
			pcpu = &per_cpu(cpuinfo, cpu);

			down_write(&pcpu->policy->rwsem);

			if (likely(down_read_trylock(&pcpu->enable_sem))) {
				if (likely(pcpu->governor_enabled))
					cpufreq_interactive_adjust_cpu(cpu,
							pcpu->policy);
				up_read(&pcpu->enable_sem);
			}

			up_write(&pcpu->policy->rwsem);
		}
	}

	return 0;
}

static void cpufreq_interactive_boost(struct cpufreq_interactive_tunables *tunables)
{
	int i;
	int anyboost = 0;
	unsigned long flags[2];
	struct cpufreq_interactive_cpuinfo *pcpu;
	struct cpumask boost_mask;
	struct cpufreq_policy *policy = container_of(tunables->policy,
						struct cpufreq_policy, policy);

	tunables->boosted = true;

	spin_lock_irqsave(&speedchange_cpumask_lock, flags[0]);

	if (have_governor_per_policy())
		cpumask_copy(&boost_mask, policy->cpus);
	else
		cpumask_copy(&boost_mask, cpu_online_mask);

	for_each_cpu(i, &boost_mask) {
		pcpu = &per_cpu(cpuinfo, i);

		if (!down_read_trylock(&pcpu->enable_sem))
			continue;

		if (!pcpu->governor_enabled) {
			up_read(&pcpu->enable_sem);
			continue;
		}

		if (tunables != pcpu->policy->governor_data) {
			up_read(&pcpu->enable_sem);
			continue;
		}

		spin_lock_irqsave(&pcpu->target_freq_lock, flags[1]);
		if (pcpu->target_freq < tunables->hispeed_freq) {
			pcpu->target_freq = tunables->hispeed_freq;
			cpumask_set_cpu(i, &speedchange_cpumask);
			pcpu->pol_hispeed_val_time =
				ktime_to_us(ktime_get());
			anyboost = 1;
		}
		spin_unlock_irqrestore(&pcpu->target_freq_lock, flags[1]);

		up_read(&pcpu->enable_sem);
	}

	spin_unlock_irqrestore(&speedchange_cpumask_lock, flags[0]);

	if (anyboost && speedchange_task)
		wake_up_process(speedchange_task);
}

static int cpufreq_interactive_notifier(
	struct notifier_block *nb, unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cpufreq_interactive_cpuinfo *pcpu;
	int cpu;
	unsigned long flags;

	if (val == CPUFREQ_PRECHANGE) {
		pcpu = &per_cpu(cpuinfo, freq->cpu);
		if (!down_read_trylock(&pcpu->enable_sem))
			return 0;
		if (!pcpu->governor_enabled) {
			up_read(&pcpu->enable_sem);
			return 0;
		}

		for_each_cpu(cpu, pcpu->policy->cpus) {
			struct cpufreq_interactive_cpuinfo *pjcpu =
				&per_cpu(cpuinfo, cpu);
			if (cpu != freq->cpu) {
				if (!down_read_trylock(&pjcpu->enable_sem))
					continue;
				if (!pjcpu->governor_enabled) {
					up_read(&pjcpu->enable_sem);
					continue;
				}
			}
			spin_lock_irqsave(&pjcpu->load_lock, flags);
			update_load(cpu);
			spin_unlock_irqrestore(&pjcpu->load_lock, flags);
			if (cpu != freq->cpu)
				up_read(&pjcpu->enable_sem);
		}

		up_read(&pcpu->enable_sem);
	}
	return 0;
}

static struct notifier_block cpufreq_notifier_block = {
	.notifier_call = cpufreq_interactive_notifier,
};

static unsigned int *get_tokenized_data(const char *buf, int *num_tokens)
{
	const char *cp;
	int i;
	int ntokens = 1;
	unsigned int *tokenized_data;
	int err = -EINVAL;

	cp = buf;
	while ((cp = strpbrk(cp + 1, " :")))
		ntokens++;

	if (!(ntokens & 0x1))
		goto err;

	tokenized_data = kmalloc(ntokens * sizeof(unsigned int), GFP_KERNEL);
	if (!tokenized_data) {
		err = -ENOMEM;
		goto err;
	}

	cp = buf;
	i = 0;
	while (i < ntokens) {
		if (sscanf(cp, "%u", &tokenized_data[i++]) != 1)
			goto err_kfree;

		cp = strpbrk(cp, " :");
		if (!cp)
			break;
		cp++;
	}

	if (i != ntokens)
		goto err_kfree;

	*num_tokens = ntokens;
	return tokenized_data;

err_kfree:
	kfree(tokenized_data);
err:
	return ERR_PTR(err);
}

static ssize_t show_target_loads(
	struct cpufreq_interactive_tunables *tunables,
	char *buf)
{
	int i;
	ssize_t ret = 0;
	unsigned long flags;
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	unsigned long flags_idx;
#endif
	spin_lock_irqsave(&tunables->target_loads_lock, flags);
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	spin_lock_irqsave(&tunables->param_index_lock, flags_idx);
	for (i = 0; i < tunables->ntarget_loads_set[tunables->param_index]; i++)
		ret += sprintf(buf + ret, "%u%s",
			tunables->target_loads_set[tunables->param_index][i],
			i & 0x1 ? ":" : " ");
	spin_unlock_irqrestore(&tunables->param_index_lock, flags_idx);
#else
	for (i = 0; i < tunables->ntarget_loads; i++)
		ret += sprintf(buf + ret, "%u%s", tunables->target_loads[i],
			       i & 0x1 ? ":" : " ");
#endif
	sprintf(buf + ret - 1, "\n");
	spin_unlock_irqrestore(&tunables->target_loads_lock, flags);
	return ret;
}

static ssize_t store_target_loads(
	struct cpufreq_interactive_tunables *tunables,
	const char *buf, size_t count)
{
	int ntokens;
	unsigned int *new_target_loads = NULL;
	unsigned long flags;
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	unsigned long flags_idx;
#endif
	new_target_loads = get_tokenized_data(buf, &ntokens);
	if (IS_ERR(new_target_loads))
		return PTR_RET(new_target_loads);

	spin_lock_irqsave(&tunables->target_loads_lock, flags);
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	spin_lock_irqsave(&tunables->param_index_lock, flags_idx);
	if (tunables->target_loads_set[tunables->param_index] != default_target_loads)
		kfree(tunables->target_loads_set[tunables->param_index]);
	tunables->target_loads_set[tunables->param_index] = new_target_loads;
	tunables->ntarget_loads_set[tunables->param_index] = ntokens;
	if (tunables->cur_param_index == tunables->param_index) {
		tunables->target_loads = new_target_loads;
		tunables->ntarget_loads = ntokens;
	}
	spin_unlock_irqrestore(&tunables->param_index_lock, flags_idx);
#else
	if (tunables->target_loads != default_target_loads)
		kfree(tunables->target_loads);
	tunables->target_loads = new_target_loads;
	tunables->ntarget_loads = ntokens;
#endif
	spin_unlock_irqrestore(&tunables->target_loads_lock, flags);

#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	spin_lock_irqsave(&tunables->mode_lock, flags_idx);
	set_new_param_set(tunables->mode, tunables);
	spin_unlock_irqrestore(&tunables->mode_lock, flags_idx);
#endif /* CONFIG_DYNAMIC_MODE_SUPPORT */

	return count;
}

static ssize_t show_above_hispeed_delay(
	struct cpufreq_interactive_tunables *tunables, char *buf)
{
	int i;
	ssize_t ret = 0;
	unsigned long flags;
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	unsigned long flags_idx;
#endif
	spin_lock_irqsave(&tunables->above_hispeed_delay_lock, flags);
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	spin_lock_irqsave(&tunables->param_index_lock, flags_idx);
	for (i = 0; i < tunables->nabove_hispeed_delay_set[tunables->param_index]; i++)
		ret += sprintf(buf + ret, "%u%s",
			tunables->above_hispeed_delay_set[tunables->param_index][i],
			i & 0x1 ? ":" : " ");
	spin_unlock_irqrestore(&tunables->param_index_lock, flags_idx);
#else
	for (i = 0; i < tunables->nabove_hispeed_delay; i++)
		ret += sprintf(buf + ret, "%u%s",
			       tunables->above_hispeed_delay[i],
			       i & 0x1 ? ":" : " ");
#endif
	sprintf(buf + ret - 1, "\n");
	spin_unlock_irqrestore(&tunables->above_hispeed_delay_lock, flags);
	return ret;
}

static ssize_t store_above_hispeed_delay(
	struct cpufreq_interactive_tunables *tunables,
	const char *buf, size_t count)
{
	int ntokens;
	unsigned int *new_above_hispeed_delay = NULL;
	unsigned long flags;
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	unsigned long flags_idx;
#endif
	new_above_hispeed_delay = get_tokenized_data(buf, &ntokens);
	if (IS_ERR(new_above_hispeed_delay))
		return PTR_RET(new_above_hispeed_delay);

	spin_lock_irqsave(&tunables->above_hispeed_delay_lock, flags);
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	spin_lock_irqsave(&tunables->param_index_lock, flags_idx);
	if (tunables->above_hispeed_delay_set[tunables->param_index] != default_above_hispeed_delay)
		kfree(tunables->above_hispeed_delay_set[tunables->param_index]);
	tunables->above_hispeed_delay_set[tunables->param_index] = new_above_hispeed_delay;
	tunables->nabove_hispeed_delay_set[tunables->param_index] = ntokens;
	if (tunables->cur_param_index == tunables->param_index) {
		tunables->above_hispeed_delay = new_above_hispeed_delay;
		tunables->nabove_hispeed_delay = ntokens;
	}
	spin_unlock_irqrestore(&tunables->param_index_lock, flags_idx);
#else
	if (tunables->above_hispeed_delay != default_above_hispeed_delay)
		kfree(tunables->above_hispeed_delay);
	tunables->above_hispeed_delay = new_above_hispeed_delay;
	tunables->nabove_hispeed_delay = ntokens;
#endif
	spin_unlock_irqrestore(&tunables->above_hispeed_delay_lock, flags);
	return count;

}

static ssize_t show_hispeed_freq(struct cpufreq_interactive_tunables *tunables,
		char *buf)
{
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	return sprintf(buf, "%u\n", tunables->hispeed_freq_set[tunables->param_index]);
#else
	return sprintf(buf, "%u\n", tunables->hispeed_freq);
#endif
}

static ssize_t store_hispeed_freq(struct cpufreq_interactive_tunables *tunables,
		const char *buf, size_t count)
{
	int ret;
	long unsigned int val;
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	unsigned long flags_idx;
#endif
	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	spin_lock_irqsave(&tunables->param_index_lock, flags_idx);
	tunables->hispeed_freq_set[tunables->param_index] = val;
	if (tunables->cur_param_index == tunables->param_index)
		tunables->hispeed_freq = val;
	spin_unlock_irqrestore(&tunables->param_index_lock, flags_idx);
#else
	tunables->hispeed_freq = val;
#endif

#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	spin_lock_irqsave(&tunables->mode_lock, flags_idx);
	set_new_param_set(tunables->mode, tunables);
	spin_unlock_irqrestore(&tunables->mode_lock, flags_idx);
#endif /* CONFIG_DYNAMIC_MODE_SUPPORT */

	return count;
}

static ssize_t show_go_hispeed_load(struct cpufreq_interactive_tunables
		*tunables, char *buf)
{
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	return sprintf(buf, "%lu\n", tunables->go_hispeed_load_set[tunables->param_index]);
#else
	return sprintf(buf, "%lu\n", tunables->go_hispeed_load);
#endif
}

static ssize_t store_go_hispeed_load(struct cpufreq_interactive_tunables
		*tunables, const char *buf, size_t count)
{
	int ret;
	unsigned long val;
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	unsigned long flags_idx;
#endif
	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	spin_lock_irqsave(&tunables->param_index_lock, flags_idx);
	tunables->go_hispeed_load_set[tunables->param_index] = val;
	if (tunables->cur_param_index == tunables->param_index)
		tunables->go_hispeed_load = val;
	spin_unlock_irqrestore(&tunables->param_index_lock, flags_idx);
#else
	tunables->go_hispeed_load = val;
#endif

#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	spin_lock_irqsave(&tunables->mode_lock, flags_idx);
	set_new_param_set(tunables->mode, tunables);
	spin_unlock_irqrestore(&tunables->mode_lock, flags_idx);
#endif /* CONFIG_DYNAMIC_MODE_SUPPORT */

	return count;
}

#ifdef CONFIG_EXYNOS_WD_DVFS
static ssize_t show_wd_boundary(struct cpufreq_interactive_tunables
		*tunables, char *buf)
{
	return sprintf(buf, "%u\n", tunables->wd_boundary);
}

static ssize_t store_wd_boundary(struct cpufreq_interactive_tunables
		*tunables, const char *buf, size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	tunables->wd_boundary = val;
	return count;
}
#endif

static ssize_t show_min_sample_time(struct cpufreq_interactive_tunables
		*tunables, char *buf)
{
	return sprintf(buf, "%lu\n", tunables->min_sample_time);
}

static ssize_t store_min_sample_time(struct cpufreq_interactive_tunables
		*tunables, const char *buf, size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	tunables->min_sample_time = val;
	return count;
}

static ssize_t show_timer_rate(struct cpufreq_interactive_tunables *tunables,
		char *buf)
{
	return sprintf(buf, "%lu\n", tunables->timer_rate);
}

static ssize_t store_timer_rate(struct cpufreq_interactive_tunables *tunables,
		const char *buf, size_t count)
{
	int ret;
	unsigned long val, val_round;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	val_round = jiffies_to_usecs(usecs_to_jiffies(val));
	if (val != val_round)
		pr_warn("timer_rate not aligned to jiffy. Rounded up to %lu\n",
			val_round);

	tunables->timer_rate = val_round;
	return count;
}

static ssize_t show_timer_slack(struct cpufreq_interactive_tunables *tunables,
		char *buf)
{
	return sprintf(buf, "%d\n", tunables->timer_slack_val);
}

static ssize_t store_timer_slack(struct cpufreq_interactive_tunables *tunables,
		const char *buf, size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	tunables->timer_slack_val = val;
	return count;
}

static ssize_t show_boost(struct cpufreq_interactive_tunables *tunables,
			  char *buf)
{
	return sprintf(buf, "%d\n", tunables->boost_val);
}

static ssize_t store_boost(struct cpufreq_interactive_tunables *tunables,
			   const char *buf, size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	tunables->boost_val = val;

	if (tunables->boost_val) {
		trace_cpufreq_interactive_boost("on");
		if (!tunables->boosted)
			cpufreq_interactive_boost(tunables);
	} else {
		tunables->boostpulse_endtime = ktime_to_us(ktime_get());
		trace_cpufreq_interactive_unboost("off");
	}

	return count;
}

static ssize_t store_boostpulse(struct cpufreq_interactive_tunables *tunables,
				const char *buf, size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	tunables->boostpulse_endtime = ktime_to_us(ktime_get()) +
		tunables->boostpulse_duration_val;
	trace_cpufreq_interactive_boost("pulse");
	if (!tunables->boosted)
		cpufreq_interactive_boost(tunables);
	return count;
}

static ssize_t show_boostpulse_duration(struct cpufreq_interactive_tunables
		*tunables, char *buf)
{
	return sprintf(buf, "%d\n", tunables->boostpulse_duration_val);
}

static ssize_t store_boostpulse_duration(struct cpufreq_interactive_tunables
		*tunables, const char *buf, size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	tunables->boostpulse_duration_val = val;
	return count;
}

static ssize_t show_io_is_busy(struct cpufreq_interactive_tunables *tunables,
		char *buf)
{
	return sprintf(buf, "%u\n", tunables->io_is_busy);
}

static ssize_t store_io_is_busy(struct cpufreq_interactive_tunables *tunables,
		const char *buf, size_t count)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	tunables->io_is_busy = val;
	return count;
}

#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
static ssize_t show_mode(struct cpufreq_interactive_tunables
		*tunables, char *buf)
{
	return sprintf(buf, "%u\n", tunables->mode);
}

static ssize_t store_mode(struct cpufreq_interactive_tunables
		*tunables, const char *buf, size_t count)
{
	int ret;
	long unsigned int val;

	ret = strict_strtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val < 0 || val >= MAX_PARAM_SET)
		return count;

	val &= PERF_MODE | SLOW_MODE | NORMAL_MODE;
	tunables->mode = val;
	return count;
}
static ssize_t show_param_index(struct cpufreq_interactive_tunables
		*tunables, char *buf)
{
	return sprintf(buf, "%u\n", tunables->param_index);
}

static ssize_t store_param_index(struct cpufreq_interactive_tunables
		*tunables, const char *buf, size_t count)
{
	int ret;
	long unsigned int val;
	unsigned long flags;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val < 0 || val >= MAX_PARAM_SET)
		return count;

	val &= PERF_MODE | SLOW_MODE | NORMAL_MODE;

	spin_lock_irqsave(&tunables->param_index_lock, flags);
	tunables->param_index = val;
	spin_unlock_irqrestore(&tunables->param_index_lock, flags);
	return count;
}
#endif

/*
 * Create show/store routines
 * - sys: One governor instance for complete SYSTEM
 * - pol: One governor instance per struct cpufreq_policy
 */
#define show_gov_pol_sys(file_name)					\
static ssize_t show_##file_name##_gov_sys				\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return show_##file_name(common_tunables, buf);			\
}									\
									\
static ssize_t show_##file_name##_gov_pol				\
(struct cpufreq_policy *policy, char *buf)				\
{									\
	return show_##file_name(policy->governor_data, buf);		\
}

#define store_gov_pol_sys(file_name)					\
static ssize_t store_##file_name##_gov_sys				\
(struct kobject *kobj, struct attribute *attr, const char *buf,		\
	size_t count)							\
{									\
	return store_##file_name(common_tunables, buf, count);		\
}									\
									\
static ssize_t store_##file_name##_gov_pol				\
(struct cpufreq_policy *policy, const char *buf, size_t count)		\
{									\
	return store_##file_name(policy->governor_data, buf, count);	\
}

#define show_store_gov_pol_sys(file_name)				\
show_gov_pol_sys(file_name);						\
store_gov_pol_sys(file_name)

show_store_gov_pol_sys(target_loads);
show_store_gov_pol_sys(above_hispeed_delay);
show_store_gov_pol_sys(hispeed_freq);
show_store_gov_pol_sys(go_hispeed_load);
show_store_gov_pol_sys(min_sample_time);
show_store_gov_pol_sys(timer_rate);
show_store_gov_pol_sys(timer_slack);
show_store_gov_pol_sys(boost);
store_gov_pol_sys(boostpulse);
show_store_gov_pol_sys(boostpulse_duration);
show_store_gov_pol_sys(io_is_busy);
#ifdef CONFIG_EXYNOS_WD_DVFS
show_store_gov_pol_sys(wd_boundary);
#endif
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
show_store_gov_pol_sys(mode);
show_store_gov_pol_sys(param_index);
#endif

#define gov_sys_attr_rw(_name)						\
static struct global_attr _name##_gov_sys =				\
__ATTR(_name, 0644, show_##_name##_gov_sys, store_##_name##_gov_sys)

#define gov_pol_attr_rw(_name)						\
static struct freq_attr _name##_gov_pol =				\
__ATTR(_name, 0644, show_##_name##_gov_pol, store_##_name##_gov_pol)

#define gov_sys_pol_attr_rw(_name)					\
	gov_sys_attr_rw(_name);						\
	gov_pol_attr_rw(_name)

gov_sys_pol_attr_rw(target_loads);
gov_sys_pol_attr_rw(above_hispeed_delay);
gov_sys_pol_attr_rw(hispeed_freq);
gov_sys_pol_attr_rw(go_hispeed_load);
gov_sys_pol_attr_rw(min_sample_time);
gov_sys_pol_attr_rw(timer_rate);
gov_sys_pol_attr_rw(timer_slack);
gov_sys_pol_attr_rw(boost);
gov_sys_pol_attr_rw(boostpulse_duration);
gov_sys_pol_attr_rw(io_is_busy);
#ifdef CONFIG_EXYNOS_WD_DVFS
gov_sys_pol_attr_rw(wd_boundary);
#endif
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
gov_sys_pol_attr_rw(mode);
gov_sys_pol_attr_rw(param_index);
#endif

static struct global_attr boostpulse_gov_sys =
	__ATTR(boostpulse, 0200, NULL, store_boostpulse_gov_sys);

static struct freq_attr boostpulse_gov_pol =
	__ATTR(boostpulse, 0200, NULL, store_boostpulse_gov_pol);

/* One Governor instance for entire system */
static struct attribute *interactive_attributes_gov_sys[] = {
	&target_loads_gov_sys.attr,
	&above_hispeed_delay_gov_sys.attr,
	&hispeed_freq_gov_sys.attr,
	&go_hispeed_load_gov_sys.attr,
	&min_sample_time_gov_sys.attr,
	&timer_rate_gov_sys.attr,
	&timer_slack_gov_sys.attr,
	&boost_gov_sys.attr,
	&boostpulse_gov_sys.attr,
	&boostpulse_duration_gov_sys.attr,
	&io_is_busy_gov_sys.attr,
#ifdef CONFIG_EXYNOS_WD_DVFS
	&wd_boundary_gov_sys.attr,
#endif
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	&mode_gov_sys.attr,
	&param_index_gov_sys.attr,
#endif
	NULL,
};

static struct attribute_group interactive_attr_group_gov_sys = {
	.attrs = interactive_attributes_gov_sys,
	.name = "interactive",
};

/* Per policy governor instance */
static struct attribute *interactive_attributes_gov_pol[] = {
	&target_loads_gov_pol.attr,
	&above_hispeed_delay_gov_pol.attr,
	&hispeed_freq_gov_pol.attr,
	&go_hispeed_load_gov_pol.attr,
	&min_sample_time_gov_pol.attr,
	&timer_rate_gov_pol.attr,
	&timer_slack_gov_pol.attr,
	&boost_gov_pol.attr,
	&boostpulse_gov_pol.attr,
	&boostpulse_duration_gov_pol.attr,
	&io_is_busy_gov_pol.attr,
#ifdef CONFIG_EXYNOS_WD_DVFS
	&wd_boundary_gov_pol.attr,
#endif
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
	&mode_gov_pol.attr,
	&param_index_gov_pol.attr,
#endif
	NULL,
};

static struct attribute_group interactive_attr_group_gov_pol = {
	.attrs = interactive_attributes_gov_pol,
	.name = "interactive",
};

static struct attribute_group *get_sysfs_attr(void)
{
	if (have_governor_per_policy())
		return &interactive_attr_group_gov_pol;
	else
		return &interactive_attr_group_gov_sys;
}

static int cpufreq_interactive_idle_notifier(struct notifier_block *nb,
					     unsigned long val,
					     void *data)
{
	if (val == IDLE_END)
		cpufreq_interactive_idle_end();

	return 0;
}

static struct notifier_block cpufreq_interactive_idle_nb = {
	.notifier_call = cpufreq_interactive_idle_notifier,
};

#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
static void cpufreq_param_set_init(struct cpufreq_interactive_tunables *tunables)
{
	unsigned int i;

	for (i = 0; i < MAX_PARAM_SET; i++) {
		tunables->target_loads_set[i] = tunables->target_loads;
		tunables->ntarget_loads_set[i] = tunables->ntarget_loads;
		tunables->hispeed_freq_set[i] = 0;
		tunables->go_hispeed_load_set[i] = tunables->go_hispeed_load;

		tunables->above_hispeed_delay_set[i] = tunables->above_hispeed_delay;
		tunables->nabove_hispeed_delay_set[i] = tunables->nabove_hispeed_delay;
	}
}
#endif

#ifdef CONFIG_EXYNOS_WD_DVFS
static int exynos_cpu_pm_notifier(struct notifier_block *self,
				  unsigned long cmd, void *v)
{
	int cpu = smp_processor_id();
	struct cpufreq_interactive_cpuinfo *pcpu =
		&per_cpu(cpuinfo, cpu);

	switch (cmd) {
	case CPU_PM_ENTER:
		stop_counter_cpu();
		read_pmu_one(&pcpu->pdata);
		break;
	case CPU_PM_EXIT:
		init_counter_cpu();
		start_counter_cpu();
		break;
	case CPU_PM_ENTER_FAILED:
		reset_pmu_one(&pcpu->pdata);
		start_counter_cpu();
		break;
	default:
		return NOTIFY_DONE;
	}
	return NOTIFY_OK;
}

static struct notifier_block exynos_cpu_pm_notifier_block = {
	.notifier_call = exynos_cpu_pm_notifier,
};
#endif

static int cpufreq_governor_interactive(struct cpufreq_policy *policy,
		unsigned int event)
{
	int rc;
	unsigned int j;
	struct cpufreq_interactive_cpuinfo *pcpu;
	struct cpufreq_frequency_table *freq_table;
	struct cpufreq_interactive_tunables *tunables;
	unsigned long flags;

	if (have_governor_per_policy())
		tunables = policy->governor_data;
	else
		tunables = common_tunables;

	WARN_ON(!tunables && (event != CPUFREQ_GOV_POLICY_INIT));

	switch (event) {
	case CPUFREQ_GOV_POLICY_INIT:
		if (have_governor_per_policy()) {
			WARN_ON(tunables);
		} else if (tunables) {
			tunables->usage_count++;
			policy->governor_data = tunables;
			return 0;
		}

		tunables = kzalloc(sizeof(*tunables), GFP_KERNEL);
		if (!tunables) {
			pr_err("%s: POLICY_INIT: kzalloc failed\n", __func__);
			return -ENOMEM;
		}

		if (!tuned_parameters[policy->cpu]) {
			tunables->above_hispeed_delay = default_above_hispeed_delay;
			tunables->nabove_hispeed_delay =
				ARRAY_SIZE(default_above_hispeed_delay);
			tunables->go_hispeed_load = DEFAULT_GO_HISPEED_LOAD;
			tunables->target_loads = default_target_loads;
			tunables->ntarget_loads = ARRAY_SIZE(default_target_loads);
			tunables->min_sample_time = DEFAULT_MIN_SAMPLE_TIME;
			tunables->timer_rate = DEFAULT_TIMER_RATE;
			tunables->boostpulse_duration_val = DEFAULT_MIN_SAMPLE_TIME;
			tunables->timer_slack_val = DEFAULT_TIMER_SLACK;
#ifdef CONFIG_EXYNOS_WD_DVFS
			tunables->wd_boundary = DEFAULT_WD_BOUNDARY;
#endif
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
			cpufreq_param_set_init(tunables);
#endif
		} else {
			memcpy(tunables, tuned_parameters[policy->cpu], sizeof(*tunables));
			kfree(tuned_parameters[policy->cpu]);
		}
		tunables->usage_count = 1;

		/* update handle for get cpufreq_policy */
		tunables->policy = &policy->policy;

		spin_lock_init(&tunables->target_loads_lock);
		spin_lock_init(&tunables->above_hispeed_delay_lock);
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
		spin_lock_init(&tunables->mode_lock);
		spin_lock_init(&tunables->param_index_lock);
#endif
		policy->governor_data = tunables;
		if (!have_governor_per_policy()) {
			common_tunables = tunables;
			WARN_ON(cpufreq_get_global_kobject());
		}

		rc = sysfs_create_group(get_governor_parent_kobj(policy),
				get_sysfs_attr());
		if (rc) {
			kfree(tunables);
			policy->governor_data = NULL;
			if (!have_governor_per_policy()) {
				common_tunables = NULL;
				cpufreq_put_global_kobject();
			}
			return rc;
		}

		if (!policy->governor->initialized) {
			idle_notifier_register(&cpufreq_interactive_idle_nb);
#ifdef CONFIG_EXYNOS_WD_DVFS
			cpu_pm_register_notifier(&exynos_cpu_pm_notifier_block);
#endif
			cpufreq_register_notifier(&cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
		}

		break;

	case CPUFREQ_GOV_POLICY_EXIT:
		if (!--tunables->usage_count) {
			if (policy->governor->initialized == 1) {
#ifdef CONFIG_EXYNOS_WD_DVFS
				cpu_pm_unregister_notifier(&exynos_cpu_pm_notifier_block);
#endif
				cpufreq_unregister_notifier(&cpufreq_notifier_block,
						CPUFREQ_TRANSITION_NOTIFIER);
				idle_notifier_unregister(&cpufreq_interactive_idle_nb);
			}

			sysfs_remove_group(get_governor_parent_kobj(policy),
					get_sysfs_attr());

			if (!have_governor_per_policy())
				cpufreq_put_global_kobject();

			tuned_parameters[policy->cpu] = kzalloc(sizeof(*tunables), GFP_KERNEL);
			if (!tuned_parameters[policy->cpu]) {
				pr_err("%s: POLICY_EXIT: kzalloc failed\n", __func__);
				return -ENOMEM;
			}
			memcpy(tuned_parameters[policy->cpu], tunables, sizeof(*tunables));
			kfree(tunables);
			common_tunables = NULL;
		}

		policy->governor_data = NULL;
		break;

	case CPUFREQ_GOV_START:
		mutex_lock(&gov_lock);

		freq_table = cpufreq_frequency_get_table(policy->cpu);
		if (!tunables->hispeed_freq)
			tunables->hispeed_freq = policy->max;
#ifdef CONFIG_DYNAMIC_MODE_SUPPORT
		for (j = 0; j < MAX_PARAM_SET; j++) {
			if (!tunables->hispeed_freq_set[j])
				tunables->hispeed_freq_set[j] = policy->max;
		}
#endif
		for_each_cpu(j, policy->cpus) {
			pcpu = &per_cpu(cpuinfo, j);
			pcpu->policy = policy;
			pcpu->target_freq = policy->cur;
			pcpu->freq_table = freq_table;
			pcpu->floor_freq = pcpu->target_freq;
			pcpu->pol_floor_val_time =
				ktime_to_us(ktime_get());
			pcpu->loc_floor_val_time = pcpu->pol_floor_val_time;
			pcpu->pol_hispeed_val_time = pcpu->pol_floor_val_time;
			pcpu->loc_hispeed_val_time = pcpu->pol_floor_val_time;
			down_write(&pcpu->enable_sem);
			del_timer_sync(&pcpu->cpu_timer);
			del_timer_sync(&pcpu->cpu_slack_timer);
			cpufreq_interactive_timer_start(tunables, j);
			pcpu->governor_enabled = 1;
			up_write(&pcpu->enable_sem);
		}

		mutex_unlock(&gov_lock);
		break;

	case CPUFREQ_GOV_STOP:
		mutex_lock(&gov_lock);
		for_each_cpu(j, policy->cpus) {
			pcpu = &per_cpu(cpuinfo, j);
			down_write(&pcpu->enable_sem);
			pcpu->governor_enabled = 0;
			del_timer_sync(&pcpu->cpu_timer);
			del_timer_sync(&pcpu->cpu_slack_timer);
			up_write(&pcpu->enable_sem);
		}

		mutex_unlock(&gov_lock);
		break;

	case CPUFREQ_GOV_LIMITS:
		if (policy->max < policy->cur)
			__cpufreq_driver_target(policy,
					policy->max, CPUFREQ_RELATION_H);
		else if (policy->min > policy->cur)
			__cpufreq_driver_target(policy,
					policy->min, CPUFREQ_RELATION_L);
		for_each_cpu(j, policy->cpus) {
			pcpu = &per_cpu(cpuinfo, j);

			down_read(&pcpu->enable_sem);
			if (pcpu->governor_enabled == 0) {
				up_read(&pcpu->enable_sem);
				continue;
			}

			spin_lock_irqsave(&pcpu->target_freq_lock, flags);
			if (policy->max < pcpu->target_freq)
				pcpu->target_freq = policy->max;
			else if (policy->min > pcpu->target_freq)
				pcpu->target_freq = policy->min;

			spin_unlock_irqrestore(&pcpu->target_freq_lock, flags);
			up_read(&pcpu->enable_sem);
		}
		break;
	}
	return 0;
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_INTERACTIVE
static
#endif
struct cpufreq_governor cpufreq_gov_interactive = {
	.name = "interactive",
	.governor = cpufreq_governor_interactive,
	.max_transition_latency = 10000000,
	.owner = THIS_MODULE,
};

static void cpufreq_interactive_nop_timer(unsigned long data)
{
}

unsigned int cpufreq_interactive_get_hispeed_freq(int cpu)
{
	struct cpufreq_interactive_cpuinfo *pcpu =
			&per_cpu(cpuinfo, cpu);
	struct cpufreq_interactive_tunables *tunables;

	if (pcpu && pcpu->policy)
		tunables = pcpu->policy->governor_data;
	else
		return 0;

	if (!tunables)
		return 0;

	return tunables->hispeed_freq;
}

#ifdef CONFIG_ARCH_EXYNOS
static int cpufreq_interactive_cluster1_min_qos_handler(struct notifier_block *b,
						unsigned long val, void *v)
{
	struct cpufreq_interactive_cpuinfo *pcpu;
	struct cpufreq_interactive_tunables *tunables;
	unsigned long flags;
	int ret = NOTIFY_OK;
	int cpu = 4; /* policy cpu of cluster 1 */

	pcpu = &per_cpu(cpuinfo, cpu);

	mutex_lock(&gov_lock);
	down_read(&pcpu->enable_sem);
	if (!pcpu->governor_enabled) {
		up_read(&pcpu->enable_sem);
		ret = NOTIFY_BAD;
		goto exit;
	}
	up_read(&pcpu->enable_sem);

	if (!pcpu->policy || !pcpu->policy->governor_data ||
		!pcpu->policy->governor) {
		ret = NOTIFY_BAD;
		goto exit;
	}

	trace_cpufreq_interactive_cpu_min_qos(cpu, val, pcpu->policy->cur);

	if (val < pcpu->policy->cur) {
		tunables = pcpu->policy->governor_data;

		spin_lock_irqsave(&speedchange_cpumask_lock, flags);
		cpumask_set_cpu(cpu, &speedchange_cpumask);
		spin_unlock_irqrestore(&speedchange_cpumask_lock, flags);

		if (speedchange_task)
			wake_up_process(speedchange_task);
	}
exit:
	mutex_unlock(&gov_lock);
	return ret;
}

static struct notifier_block cpufreq_interactive_cluster1_min_qos_notifier = {
	.notifier_call = cpufreq_interactive_cluster1_min_qos_handler,
};

static int cpufreq_interactive_cluster1_max_qos_handler(struct notifier_block *b,
						unsigned long val, void *v)
{
	struct cpufreq_interactive_cpuinfo *pcpu;
	struct cpufreq_interactive_tunables *tunables;
	unsigned long flags;
	int ret = NOTIFY_OK;
	int cpu = 4; /* policy cpu of cluster1 */

	pcpu = &per_cpu(cpuinfo, cpu);

	mutex_lock(&gov_lock);
	down_read(&pcpu->enable_sem);
	if (!pcpu->governor_enabled) {
		up_read(&pcpu->enable_sem);
		ret =  NOTIFY_BAD;
		goto exit;
	}
	up_read(&pcpu->enable_sem);

	if (!pcpu->policy || !pcpu->policy->governor_data ||
		!pcpu->policy->governor) {
		ret = NOTIFY_BAD;
		goto exit;
	}

	trace_cpufreq_interactive_cpu_max_qos(cpu, val, pcpu->policy->cur);

	if (val > pcpu->policy->cur) {
		tunables = pcpu->policy->governor_data;

		spin_lock_irqsave(&speedchange_cpumask_lock, flags);
		cpumask_set_cpu(cpu, &speedchange_cpumask);
		spin_unlock_irqrestore(&speedchange_cpumask_lock, flags);

		if (speedchange_task)
			wake_up_process(speedchange_task);
	}
exit:
	mutex_unlock(&gov_lock);
	return ret;
}

static struct notifier_block cpufreq_interactive_cluster1_max_qos_notifier = {
	.notifier_call = cpufreq_interactive_cluster1_max_qos_handler,
};

static int cpufreq_interactive_cluster0_min_qos_handler(struct notifier_block *b,
						unsigned long val, void *v)
{
	struct cpufreq_interactive_cpuinfo *pcpu;
	struct cpufreq_interactive_tunables *tunables;
	unsigned long flags;
	int ret = NOTIFY_OK;
	int cpu = 0; /* policy cpu of cluster0 */

	pcpu = &per_cpu(cpuinfo, cpu);

	mutex_lock(&gov_lock);
	down_read(&pcpu->enable_sem);
	if (!pcpu->governor_enabled) {
		up_read(&pcpu->enable_sem);
		ret = NOTIFY_BAD;
		goto exit;
	}
	up_read(&pcpu->enable_sem);

	if (!pcpu->policy || !pcpu->policy->governor_data ||
		!pcpu->policy->governor) {
		ret = NOTIFY_BAD;
		goto exit;
	}

	trace_cpufreq_interactive_cpu_min_qos(cpu, val, pcpu->policy->cur);

	if (val < pcpu->policy->cur) {
		tunables = pcpu->policy->governor_data;

		spin_lock_irqsave(&speedchange_cpumask_lock, flags);
		cpumask_set_cpu(cpu, &speedchange_cpumask);
		spin_unlock_irqrestore(&speedchange_cpumask_lock, flags);

		if (speedchange_task)
			wake_up_process(speedchange_task);
	}
exit:
	mutex_unlock(&gov_lock);
	return ret;
}

static struct notifier_block cpufreq_interactive_cluster0_min_qos_notifier = {
	.notifier_call = cpufreq_interactive_cluster0_min_qos_handler,
};

static int cpufreq_interactive_cluster0_max_qos_handler(struct notifier_block *b,
						unsigned long val, void *v)
{
	struct cpufreq_interactive_cpuinfo *pcpu;
	struct cpufreq_interactive_tunables *tunables;
	unsigned long flags;
	int ret = NOTIFY_OK;
	int cpu = 0; /* policy cpu of cluster0 */

	pcpu = &per_cpu(cpuinfo, cpu);

	mutex_lock(&gov_lock);
	down_read(&pcpu->enable_sem);
	if (!pcpu->governor_enabled) {
		up_read(&pcpu->enable_sem);
		ret = NOTIFY_BAD;
		goto exit;
	}
	up_read(&pcpu->enable_sem);

	if (!pcpu->policy ||!pcpu->policy->governor_data ||
		!pcpu->policy->governor) {
		ret = NOTIFY_BAD;
		goto exit;
	}

	trace_cpufreq_interactive_cpu_max_qos(cpu, val, pcpu->policy->cur);

	if (val > pcpu->policy->cur) {
		tunables = pcpu->policy->governor_data;

		spin_lock_irqsave(&speedchange_cpumask_lock, flags);
		cpumask_set_cpu(cpu, &speedchange_cpumask);
		spin_unlock_irqrestore(&speedchange_cpumask_lock, flags);

		if (speedchange_task)
			wake_up_process(speedchange_task);
	}
exit:
	mutex_unlock(&gov_lock);
	return ret;
}

static struct notifier_block cpufreq_interactive_cluster0_max_qos_notifier = {
	.notifier_call = cpufreq_interactive_cluster0_max_qos_handler,
};
#endif

static int __init cpufreq_interactive_init(void)
{
	unsigned int i;
	struct cpufreq_interactive_cpuinfo *pcpu;

	/* Initalize per-cpu timers */
	for_each_possible_cpu(i) {
		pcpu = &per_cpu(cpuinfo, i);
		init_timer_deferrable(&pcpu->cpu_timer);
		pcpu->cpu_timer.function = cpufreq_interactive_timer;
		pcpu->cpu_timer.data = i;
		init_timer(&pcpu->cpu_slack_timer);
		pcpu->cpu_slack_timer.function = cpufreq_interactive_nop_timer;
		spin_lock_init(&pcpu->load_lock);
		spin_lock_init(&pcpu->target_freq_lock);
		init_rwsem(&pcpu->enable_sem);
	}

	spin_lock_init(&speedchange_cpumask_lock);
	mutex_init(&gov_lock);

	speedchange_task =
		kthread_create(cpufreq_interactive_speedchange_task, NULL,
				"cfinteractive");
	if (IS_ERR(speedchange_task))
		return PTR_ERR(speedchange_task);

	kthread_bind(speedchange_task, 0);

#ifdef CONFIG_ARCH_EXYNOS
	pm_qos_add_notifier(PM_QOS_CLUSTER1_FREQ_MIN, &cpufreq_interactive_cluster1_min_qos_notifier);
	pm_qos_add_notifier(PM_QOS_CLUSTER1_FREQ_MAX, &cpufreq_interactive_cluster1_max_qos_notifier);
	pm_qos_add_notifier(PM_QOS_CLUSTER0_FREQ_MIN, &cpufreq_interactive_cluster0_min_qos_notifier);
	pm_qos_add_notifier(PM_QOS_CLUSTER0_FREQ_MAX, &cpufreq_interactive_cluster0_max_qos_notifier);
#endif

	return cpufreq_register_governor(&cpufreq_gov_interactive);
}

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_INTERACTIVE
fs_initcall(cpufreq_interactive_init);
#else
module_init(cpufreq_interactive_init);
#endif

static void __exit cpufreq_interactive_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_interactive);
}

module_exit(cpufreq_interactive_exit);

MODULE_AUTHOR("Mike Chan <mike@android.com>");
MODULE_DESCRIPTION("'cpufreq_interactive' - A cpufreq governor for "
	"Latency sensitive workloads");
MODULE_LICENSE("GPL");

/*
 * moro_sound.c  --  Sound mod for Moon, S7 sound driver
 *
 * Author	: @morogoku https://github.com/morogoku
 * 
 * Date		: March 2019 - v2.0
 *		: April 2019 - v2.1
 *		: December 2020 - v2.2.0
 *
 *
 * Based on the Boeffla Sound 1.6 for Galaxy S3
 *
 * Credits: 	andip71, author of Boeffla Sound
 *		Supercurio, Yank555 and Gokhanmoral.
 *
 *		AndreiLux, for his Arizona control sound mod
 *		
 *		Flar2, for his speaker gain mod
 *
 */


#include "moro_sound.h"


/*****************************************/
// Variables
/*****************************************/

// pointer to regmap
static struct regmap *map;

// First access
static int first = 1;

// Switches
static int moro_sound, dual_speaker, out3_ena, eq, headphone_mono, mic, reset = 0;

// Gains
static int headphone_gain_l, headphone_gain_r, earpiece_gain, speaker_gain, eq_gains[5],
	mic_down_gain, mic_up_gain, mic_hp_gain;

// Mixers
static int out1l_mix_source, out1r_mix_source, out3l_mix_source, out3r_mix_source,
	eq1_mix_source, eq2_mix_source;


/*****************************************/
// Internal function declarations
/*****************************************/

static unsigned int get_value(int reg, int mask, int shift);
static void set_value(int reg, int mask, int shift, int value);

static void set_eq(bool reset);
static void set_eq_gains(bool reset);
static void set_dual_speaker(void);

static void reset_moro_sound(void);
static void reset_audio_hub(void);
static void update_audio_hub(void);


/*****************************************/
// Internal helper functions
/*****************************************/

#define moon_write(reg, val) _regmap_write_nohook(map, reg, val)

#define moon_read(reg, val) regmap_read(map, reg, val)

static unsigned int get_value(int reg, int mask, int shift)
{
	unsigned int val;

	moon_read(reg, &val);
	val &= mask;
	val >>= shift;

	return val;
}

static void set_value(int reg, int mask, int shift, int value)
{
	unsigned int val;

	moon_read(reg, &val);
	val &= ~mask;
	val |= (value << shift);
	moon_write(reg, val);
}

static void set_eq(bool reset)
{
	// If EQ is enabled
	if (eq & moro_sound) {
		// Set mixers
		eq1_mix_source = 32;	// EQ1 -> AIF1 RX1 left
		eq2_mix_source = 33;	// EQ2 -> AIF1 RX2 right

		out1l_mix_source = 80;	// OUT1L -> EQ1 left
		out1r_mix_source = 81;	// OUT1R -> EQ2 right
	} else {
		// Set mixers to default
		eq1_mix_source = EQ1_MIX_DEFAULT;
		eq2_mix_source = EQ2_MIX_DEFAULT;

		out1l_mix_source = OUT1L_MIX_DEFAULT;
		out1r_mix_source = OUT1R_MIX_DEFAULT;
	}
	
	set_value(EQ1_ENA, eq);
	set_value(EQ2_ENA, eq);

	set_value(EQ1_MIX, eq1_mix_source);
	set_value(EQ2_MIX, eq2_mix_source);

	set_value(OUT1L_MIX, out1l_mix_source);
	set_value(OUT1R_MIX, out1r_mix_source);

	// If reset = true, then set the eq band gains to default values
	if (reset)
		set_eq_gains(1);
	else
		set_eq_gains(0);
}

static void set_eq_gains(bool reset)
{
	unsigned int val;
	unsigned int gain1, gain2, gain3, gain4, gain5;

	// If reset = true, set the eq band gains to default
	if (reset) {
		gain1 = EQ_B1_GAIN_DEFAULT;
		gain2 = EQ_B2_GAIN_DEFAULT;
		gain3 = EQ_B3_GAIN_DEFAULT;
		gain4 = EQ_B4_GAIN_DEFAULT;
		gain5 = EQ_B5_GAIN_DEFAULT;
	} else {
		gain1 = eq_gains[0];
		gain2 = eq_gains[1];
		gain3 = eq_gains[2];
		gain4 = eq_gains[3];
		gain5 = eq_gains[4];
	}

	// First register
	// read current value from audio hub and mask all bits apart from equalizer enabled bit,
	// add individual gains and write back to audio hub
	moon_read(ARIZONA_EQ1_1, &val);
	val &= ARIZONA_EQ1_ENA_MASK;
	val |= ((gain1 + EQ_GAIN_OFFSET) << ARIZONA_EQ1_B1_GAIN_SHIFT);
	val |= ((gain2 + EQ_GAIN_OFFSET) << ARIZONA_EQ1_B2_GAIN_SHIFT);
	val |= ((gain3 + EQ_GAIN_OFFSET) << ARIZONA_EQ1_B3_GAIN_SHIFT);
	moon_write(ARIZONA_EQ1_1, val);
	moon_write(ARIZONA_EQ2_1, val);

	// second register
	// read current value from audio hub and mask all bits apart from band1 mode bit,
	// set individual gains and write back to audio hub
	moon_read(ARIZONA_EQ1_2, &val);
	val &= ARIZONA_EQ1_B1_MODE_MASK;
	val |= ((gain4 + EQ_GAIN_OFFSET) << ARIZONA_EQ1_B4_GAIN_SHIFT);
	val |= ((gain5 + EQ_GAIN_OFFSET) << ARIZONA_EQ1_B5_GAIN_SHIFT);
	moon_write(ARIZONA_EQ1_2, val);
	moon_write(ARIZONA_EQ2_2, val);
}


static void set_dual_speaker(void)
{
	// if dual speaker is on
	if (dual_speaker & moro_sound) {
		// Enable earpiece
		out3_ena = 1;

		// set earpiece mixer to AIF1RX1 and AIF1RX2
		out3l_mix_source = 32;
		out3r_mix_source = 33;
	} else {
		// Disable earpiece
		out3_ena = 0;

		// set earpiece mixer to DEFAULT
		out3l_mix_source = 0;
		out3r_mix_source = 0;
	}
	
	set_value(OUT3L_ENA, out3_ena);
	set_value(OUT3R_ENA, out3_ena);

	set_value(OUT3L_MIX, out3l_mix_source);
	set_value(OUT3R_MIX, out3r_mix_source);
}


/*****************************************/
// Sound hook functions
/*****************************************/

void moro_sound_hook_moon_pcm_probe(struct regmap *pmap)
{
	// store a copy of the pointer to the regmap, we need
	// that for internal calls to the audio hub
	map = pmap;

	// Initialize moro sound master switch finally
	moro_sound = MORO_SOUND_DEFAULT;
	headphone_mono = HEADPHONE_MONO_DEFAULT;
	dual_speaker = DUAL_SPEAKER_DEFAULT;
	mic = MIC_DEFAULT;
	eq = EQ_DEFAULT;
	eq_gains[0] = EQ_B1_GAIN_DEFAULT;
	eq_gains[1] = EQ_B2_GAIN_DEFAULT;
	eq_gains[2] = EQ_B3_GAIN_DEFAULT;
	eq_gains[3] = EQ_B4_GAIN_DEFAULT;
	eq_gains[4] = EQ_B5_GAIN_DEFAULT;
	set_eq(0);
	set_dual_speaker();

	// If moro sound is enabled during driver start, reset to default configuration
	if (moro_sound)
		reset_moro_sound();
}

unsigned int moro_sound_write_hook(unsigned int reg, unsigned int val)
{
	// if moro sound is off, return original value
	if (!moro_sound)
		return val;

	// based on the register, do the appropriate processing
	switch (reg) {
	case ARIZONA_DAC_DIGITAL_VOLUME_1L: // headphone l
		val &= ~ARIZONA_OUT1L_VOL_MASK;
		val |= (headphone_gain_l << ARIZONA_OUT1L_VOL_SHIFT);
		return val;
	case ARIZONA_DAC_DIGITAL_VOLUME_1R: // headphone r
		val &= ~ARIZONA_OUT1R_VOL_MASK;
		val |= (headphone_gain_r << ARIZONA_OUT1R_VOL_SHIFT);
		return val;
	case ARIZONA_DAC_DIGITAL_VOLUME_3L: // earpiece
		val &= ~ARIZONA_OUT3L_VOL_MASK;
		val |= (earpiece_gain << ARIZONA_OUT3L_VOL_SHIFT);
		return val;
	}

	if (eq) {
		switch (reg) {
		case ARIZONA_OUT1LMIX_INPUT_1_SOURCE: // headphone l source
			val &= ~ARIZONA_MIXER_SOURCE_MASK;
			val |= (out1l_mix_source << ARIZONA_MIXER_SOURCE_SHIFT);
			return val;
		case ARIZONA_OUT1RMIX_INPUT_1_SOURCE: // headphone r source
			val &= ~ARIZONA_MIXER_SOURCE_MASK;
			val |= (out1r_mix_source << ARIZONA_MIXER_SOURCE_SHIFT);
			return val;
		}
	}
	
	if (dual_speaker) {
		switch (reg) {
		case ARIZONA_OUTPUT_ENABLES_1: // earpiece enable
			val &= ~(ARIZONA_OUT3L_ENA_MASK + ARIZONA_OUT3R_ENA_MASK);
			val |= ((out3_ena << ARIZONA_OUT3L_ENA_SHIFT) | (out3_ena << ARIZONA_OUT3R_ENA_SHIFT));
			return val;
		case ARIZONA_OUT3LMIX_INPUT_1_SOURCE: // earpiece l source
			val &= ~ARIZONA_MIXER_SOURCE_MASK;
			val |= (out3l_mix_source << ARIZONA_MIXER_SOURCE_SHIFT);
			return val;
		case ARIZONA_OUT3RMIX_INPUT_1_SOURCE: // earpiece r source
			val &= ~ARIZONA_MIXER_SOURCE_MASK;
			val |= (out3r_mix_source << ARIZONA_MIXER_SOURCE_SHIFT);
			return val;
		}
	}

	if (mic) {
		switch (reg) {
		case ARIZONA_ADC_DIGITAL_VOLUME_1R:  // mic down
			val &= ~ARIZONA_IN1R_DIG_VOL_MASK;
			val |= (mic_down_gain << ARIZONA_IN1R_DIG_VOL_SHIFT);
			return val;
		case ARIZONA_ADC_DIGITAL_VOLUME_3L:  // mic up
			val &= ~ARIZONA_IN3L_DIG_VOL_MASK;
			val |= (mic_up_gain << ARIZONA_IN3L_DIG_VOL_SHIFT);
			return val;
		case ARIZONA_ADC_DIGITAL_VOLUME_2L:  // mic hp
			val &= ~ARIZONA_IN2L_DIG_VOL_MASK;
			val |= (mic_hp_gain << ARIZONA_IN2L_DIG_VOL_SHIFT);
			return val;
		}
	}

	return val;
}


/*****************************************/
// Initialization functions
/*****************************************/

static void reset_moro_sound(void)
{
	// set all moro sound config settings to defaults

	headphone_gain_l = HEADPHONE_DEFAULT;
	headphone_gain_r = HEADPHONE_DEFAULT;
	headphone_mono = HEADPHONE_MONO_DEFAULT;

	earpiece_gain = EARPIECE_DEFAULT;

	speaker_gain = SPEAKER_DEFAULT;

	out1l_mix_source = OUT1L_MIX_DEFAULT;
	out1r_mix_source = OUT1R_MIX_DEFAULT;

	dual_speaker = DUAL_SPEAKER_DEFAULT;

	eq1_mix_source = EQ1_MIX_DEFAULT;
	eq2_mix_source = EQ2_MIX_DEFAULT;
	
	eq = EQ_DEFAULT;
	eq_gains[0] = EQ_B1_GAIN_DEFAULT;
	eq_gains[1] = EQ_B2_GAIN_DEFAULT;
	eq_gains[2] = EQ_B3_GAIN_DEFAULT;
	eq_gains[3] = EQ_B4_GAIN_DEFAULT;
	eq_gains[4] = EQ_B5_GAIN_DEFAULT;

	mic = MIC_DEFAULT;
	mic_down_gain = MIC_DOWN_GAIN_DEFAULT;
	mic_up_gain = MIC_UP_GAIN_DEFAULT;
	mic_hp_gain = MIC_HP_GAIN_DEFAULT;
}

static void reset_audio_hub(void)
{
	// reset all audio hub registers back to defaults

	set_value(OUT1L_VOLUME, HEADPHONE_DEFAULT);
	set_value(OUT1R_VOLUME, HEADPHONE_DEFAULT);
	set_value(OUT1_MONO, HEADPHONE_MONO_DEFAULT);
	
	set_value(OUT3L_VOLUME, EARPIECE_DEFAULT);

	set_speaker_gain(SPEAKER_DEFAULT);

	set_value(OUT1L_MIX, OUT1L_MIX_DEFAULT);
	set_value(OUT1R_MIX, OUT1R_MIX_DEFAULT);

	set_value(EQ1_MIX, EQ1_MIX_DEFAULT);
	set_value(EQ2_MIX, EQ2_MIX_DEFAULT);
	
	set_value(MIC1R_VOLUME, MIC_DOWN_GAIN_DEFAULT);
	set_value(MIC3L_VOLUME, MIC_UP_GAIN_DEFAULT);
	set_value(MIC2L_VOLUME, MIC_HP_GAIN_DEFAULT);

	set_eq(1);
	set_dual_speaker();
}

static void update_audio_hub(void)
{
	// reset all audio hub registers back to defaults

	set_value(OUT1L_VOLUME, headphone_gain_l);
	set_value(OUT1R_VOLUME, headphone_gain_r);
	set_value(OUT1_MONO, headphone_mono);

	set_value(OUT3L_VOLUME, earpiece_gain);

	set_speaker_gain(speaker_gain);

	set_value(OUT1L_MIX, out1l_mix_source);
	set_value(OUT1R_MIX, out1r_mix_source);

	set_value(EQ1_MIX, eq1_mix_source);
	set_value(EQ2_MIX, eq1_mix_source);
	
	set_value(MIC1R_VOLUME, mic_down_gain);
	set_value(MIC3L_VOLUME, mic_up_gain);
	set_value(MIC2L_VOLUME, mic_hp_gain);

	set_eq(0);
	set_dual_speaker();
}


/*****************************************/
// sysfs interface functions
/*****************************************/

// Moro sound master switch

static ssize_t moro_sound_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", moro_sound);
}

static ssize_t moro_sound_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	// store if valid data
	if ((val == 0) || (val == 1)) {
		// check if there was a change
		if (moro_sound != val) {
			// set new status
			moro_sound = val;

			// re-initialize settings and audio hub (in any case for both on and off !)
			// if is the first enable, reset variables
			if (first) {
				reset_moro_sound();
				first = 0;
			}

			if (val == 1) {
				update_audio_hub();
			} else {
				reset_audio_hub();
			}
		}
	}

	return count;
}


// Headphone volume

static ssize_t headphone_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d %d\n", headphone_gain_l, headphone_gain_r);
}

static ssize_t headphone_gain_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val_l, val_r;

	// Terminate if moro sound is not enabled
	if (!moro_sound)
		return count;

	// read values from input buffer
	ret = sscanf(buf, "%d %d", &val_l, &val_r);

	if (ret != 2)
		return -EINVAL;

	if (val_l < HEADPHONE_MIN)
		val_l = HEADPHONE_MIN;

	if (val_l > HEADPHONE_MAX)
		val_l = HEADPHONE_MAX;

	if (val_r < HEADPHONE_MIN)
		val_r = HEADPHONE_MIN;

	if (val_r > HEADPHONE_MAX)
		val_r = HEADPHONE_MAX;

	// store new values
	headphone_gain_l = val_l;
	headphone_gain_r = val_r;

	// set new values
	set_value(OUT1L_VOLUME, headphone_gain_l);
	set_value(OUT1R_VOLUME, headphone_gain_r);

	return count;
}

static ssize_t headphone_mono_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", headphone_mono);
}

static ssize_t headphone_mono_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// Terminate if moro sound is not enabled
	if (!moro_sound)
		return count;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	// store if valid data
	if ((val == 0) || (val == 1)) {
		// check if there was a change
		if (headphone_mono != val) {
			// set new status
			headphone_mono = val;

			// set new values
			set_value(OUT1_MONO, headphone_mono);
		}
	}

	return count;
}

static ssize_t headphone_limits_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Min:%u Max:%u Def:%u\n", HEADPHONE_MIN, HEADPHONE_MAX, HEADPHONE_DEFAULT);
}


// Earpiece Volume

static ssize_t earpiece_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", earpiece_gain);
}

static ssize_t earpiece_gain_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// Terminate if moro sound is not enabled
	if (!moro_sound)
		return count;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if (val < EARPIECE_MIN)
		val = EARPIECE_MIN;

	if (val > EARPIECE_MAX)
		val = EARPIECE_MAX;

	// store new values
	earpiece_gain = val;

	// set new values
	set_value(OUT3L_VOLUME, earpiece_gain);

	return count;
}

static ssize_t earpiece_limits_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Min:%u Max:%u Def:%u\n", EARPIECE_MIN, EARPIECE_MAX, EARPIECE_DEFAULT);
}


// Dual Speaker

static ssize_t dual_speaker_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", dual_speaker);
}

static ssize_t dual_speaker_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// Terminate if moro sound is not enabled
	if (!moro_sound)
		return count;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	// store if valid data
	if ((val == 0) || (val == 1)) {
		// check if there was a change
		if (dual_speaker != val) {
			// set new status
			dual_speaker = val;

			// set new values
			set_dual_speaker();
		}
	}

	return count;
}


// Mic Gain

static ssize_t mic_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", mic);
}

static ssize_t mic_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// Terminate if moro sound is not enabled
	if (!moro_sound)
		return count;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	mic = val;
	
	set_value(MIC1R_VOLUME, MIC_DOWN_GAIN_DEFAULT);
	set_value(MIC3L_VOLUME, MIC_UP_GAIN_DEFAULT);
	set_value(MIC2L_VOLUME, MIC_HP_GAIN_DEFAULT);
	
	return count;
}

static ssize_t mic_down_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", mic_down_gain);
}

static ssize_t mic_down_gain_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// Terminate if moro sound is not enabled
	if ((!moro_sound) && (!mic))
		return count;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	mic_down_gain = val;

	set_value(MIC1R_VOLUME, mic_down_gain);

	return count;
}

static ssize_t mic_up_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", mic_up_gain);
}

static ssize_t mic_up_gain_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// Terminate if moro sound is not enabled
	if ((!moro_sound) && (!mic))
		return count;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	mic_up_gain = val;

	set_value(MIC3L_VOLUME, mic_up_gain);

	return count;
}

static ssize_t mic_hp_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", mic_hp_gain);
}

static ssize_t mic_hp_gain_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// Terminate if moro sound is not enabled
	if ((!moro_sound) && (!mic))
		return count;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	mic_hp_gain = val;

	set_value(MIC2L_VOLUME, mic_hp_gain);

	return count;
}


// Speaker Volume

static ssize_t speaker_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", speaker_gain);
}

static ssize_t speaker_gain_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if (val < SPEAKER_MIN)
		val = SPEAKER_MIN;

	if (val > SPEAKER_MAX)
		val = SPEAKER_MAX;

	// store new values
	speaker_gain = val;

	// set new values
	set_speaker_gain(speaker_gain);

	return count;
}

static ssize_t speaker_limits_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Min:%u Max:%u Def:%u\n", SPEAKER_MIN, SPEAKER_MAX, SPEAKER_DEFAULT);
}


// EQ

static ssize_t eq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", eq);
}

static ssize_t eq_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// Terminate if moro sound is not enabled
	if (!moro_sound)
		return count;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	// store if valid data
	if ((val == 0) || (val == 1)) {
		// check if there was a change
		if (eq != val) {
			// store new value
			eq = val;
			// set eq values
			set_eq(0);
		}
	}

	return count;
}


// EQ GAIN

static ssize_t eq_gains_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d %d %d %d %d\n", eq_gains[0], eq_gains[1], eq_gains[2], eq_gains[3], eq_gains[4]);
}

static ssize_t eq_gains_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int i, gains[5];

	// Terminate if moro sound is not enabled
	if (!moro_sound)
		return count;

	// read values from input buffer
	ret = sscanf(buf, "%d %d %d %d %d", &gains[0], &gains[1], &gains[2], &gains[3], &gains[4]);

	if (ret != 5)
		return -EINVAL;

	// check validity of gain values and adjust
	for (i = 0; i <= 4; i++) {
		if (gains[i] < EQ_GAIN_MIN)
			gains[i] = EQ_GAIN_MIN;

		if (gains[i] > EQ_GAIN_MAX)
			gains[i] = EQ_GAIN_MAX;

		eq_gains[i] = gains[i];
	}

	// set new values
	set_eq_gains(0);

	return count;
}

static ssize_t eq_b1_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", eq_gains[0]);
}

static ssize_t eq_b1_gain_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// check data and store if valid
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if (val < EQ_GAIN_MIN)
		val = EQ_GAIN_MIN;

	if (val > EQ_GAIN_MAX)
		val = EQ_GAIN_MAX;

	// store new value
	eq_gains[0] = val;

	// set new values
	set_eq_gains(0);

	return count;
}

static ssize_t eq_b2_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", eq_gains[1]);
}

static ssize_t eq_b2_gain_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// check data and store if valid
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if (val < EQ_GAIN_MIN)
		val = EQ_GAIN_MIN;

	if (val > EQ_GAIN_MAX)
		val = EQ_GAIN_MAX;

	// store new value
	eq_gains[1] = val;

	// set new values
	set_eq_gains(0);

	return count;
}

static ssize_t eq_b3_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", eq_gains[2]);
}

static ssize_t eq_b3_gain_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// check data and store if valid
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if (val < EQ_GAIN_MIN)
		val = EQ_GAIN_MIN;

	if (val > EQ_GAIN_MAX)
		val = EQ_GAIN_MAX;

	// store new value
	eq_gains[2] = val;

	// set new values
	set_eq_gains(0);

	return count;
}

static ssize_t eq_b4_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", eq_gains[3]);
}

static ssize_t eq_b4_gain_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// check data and store if valid
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if (val < EQ_GAIN_MIN)
		val = EQ_GAIN_MIN;

	if (val > EQ_GAIN_MAX)
		val = EQ_GAIN_MAX;

	// store new value
	eq_gains[3] = val;

	// set new values
	set_eq_gains(0);

	return count;
}

static ssize_t eq_b5_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", eq_gains[4]);
}

static ssize_t eq_b5_gain_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// check data and store if valid
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if (val < EQ_GAIN_MIN)
		val = EQ_GAIN_MIN;

	if (val > EQ_GAIN_MAX)
		val = EQ_GAIN_MAX;
	
	// store new value
	eq_gains[4] = val;

	// set new values
	set_eq_gains(0);

	return count;
}


// Reset

static ssize_t reset_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", reset);
}

static ssize_t reset_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// check data and store if valid
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if (val == 1) {
		reset_moro_sound();
		update_audio_hub();
	}

	return count;
}


// Register dump

static ssize_t reg_dump_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned int out3l_mix2, out3r_mix2, eq_b1, eq_b2, eq_b3, eq_b4, eq_b5, mic1r, mic3l, mic2l;

	moon_read(ARIZONA_OUT3LMIX_INPUT_2_SOURCE, &out3l_mix2);

	moon_read(ARIZONA_OUT3RMIX_INPUT_2_SOURCE, &out3r_mix2);

	moon_read(ARIZONA_EQ1_1, &eq_b1);
		eq_b1 = ((eq_b1 & ARIZONA_EQ1_B1_GAIN_MASK) >> ARIZONA_EQ1_B1_GAIN_SHIFT) - EQ_GAIN_OFFSET;
	moon_read(ARIZONA_EQ1_1, &eq_b2);
		eq_b2 = ((eq_b2 & ARIZONA_EQ1_B2_GAIN_MASK) >> ARIZONA_EQ1_B2_GAIN_SHIFT) - EQ_GAIN_OFFSET;
	moon_read(ARIZONA_EQ1_1, &eq_b3);
		eq_b3 = ((eq_b3 & ARIZONA_EQ1_B3_GAIN_MASK) >> ARIZONA_EQ1_B3_GAIN_SHIFT) - EQ_GAIN_OFFSET;
	moon_read(ARIZONA_EQ1_2, &eq_b4);
		eq_b4 = ((eq_b4 & ARIZONA_EQ1_B4_GAIN_MASK) >> ARIZONA_EQ1_B4_GAIN_SHIFT) - EQ_GAIN_OFFSET;
	moon_read(ARIZONA_EQ1_2, &eq_b5);
		eq_b5 = ((eq_b5 & ARIZONA_EQ1_B5_GAIN_MASK) >> ARIZONA_EQ1_B5_GAIN_SHIFT) - EQ_GAIN_OFFSET;
	
	moon_read(ARIZONA_INPUT_ENABLES, &mic1r);
		mic1r = (mic1r & ARIZONA_IN1R_ENA_MASK) >> ARIZONA_IN1R_ENA_SHIFT;
	moon_read(ARIZONA_INPUT_ENABLES, &mic3l);
		mic3l = (mic3l & ARIZONA_IN3L_ENA_MASK) >> ARIZONA_IN3L_ENA_SHIFT;
	moon_read(ARIZONA_INPUT_ENABLES, &mic2l);
		mic2l = (mic2l & ARIZONA_IN2L_ENA_MASK) >> ARIZONA_IN2L_ENA_SHIFT;


	// return register dump information
	return sprintf(buf, "\
headphone_gain_l: reg: %d, variable: %d\n\
headphone_gain_r: reg: %d, variable: %d\n\
headphone mono: %d\n\
first enable: %d\n\
earpiece_gain: %d\n\
speaker_gain: %d\n\
OUT1 Source: L: %d R: %d\n\
OUT3 Enabled: L:%d R: %d\n\
OUT3 MIX1: L: %d R: %d\n\
OUT3 MIX2: L: %d R: %d\n\
EQ Enabled: 1: %d 2: %d\n\
EQMIX source: 1: %d 2: %d\n\
EQ b1 gain: %d\n\
EQ b2 gain: %d\n\
EQ b3 gain: %d\n\
EQ b4 gain: %d\n\
EQ b5 gain: %d\n\
MIC Down: %d Vol: %d\n\
MIC Up: %d Vol: %d\n\
MIC Hp: %d Vol: %d\n\
",
get_value(OUT1L_VOLUME), headphone_gain_l,
get_value(OUT1R_VOLUME), headphone_gain_r,
get_value(OUT1_MONO),
first,
get_value(OUT3L_VOLUME),
get_speaker_gain(),
get_value(OUT1L_MIX), get_value(OUT1R_MIX),
get_value(OUT3L_ENA), get_value(OUT3R_ENA),
get_value(OUT3L_MIX), get_value(OUT3R_MIX),
out3l_mix2, out3r_mix2,
get_value(EQ1_ENA), get_value(EQ2_ENA),
get_value(EQ1_MIX), get_value(EQ2_MIX),
eq_b1,
eq_b2,
eq_b3,
eq_b4,
eq_b5,
mic1r,
get_value(MIC1R_VOLUME),
mic3l,
get_value(MIC3L_VOLUME),
mic2l,
get_value(MIC2L_VOLUME));
}


// Version information

static ssize_t version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// return version information
	return sprintf(buf, "%s\n", MORO_SOUND_VERSION);
}


/*****************************************/
// Initialize moro sound sysfs folder
/*****************************************/

// define objects
static DEVICE_ATTR(moro_sound, 0664, moro_sound_show, moro_sound_store);
static DEVICE_ATTR(headphone_gain, 0664, headphone_gain_show, headphone_gain_store);
static DEVICE_ATTR(headphone_mono, 0664, headphone_mono_show, headphone_mono_store);
static DEVICE_ATTR(headphone_limits, 0664, headphone_limits_show, NULL);
static DEVICE_ATTR(earpiece_gain, 0664, earpiece_gain_show, earpiece_gain_store);
static DEVICE_ATTR(earpiece_limits, 0664, earpiece_limits_show, NULL);
static DEVICE_ATTR(mic, 0664, mic_show, mic_store);
static DEVICE_ATTR(mic_down_gain, 0664, mic_down_gain_show, mic_down_gain_store);
static DEVICE_ATTR(mic_up_gain, 0664, mic_up_gain_show, mic_up_gain_store);
static DEVICE_ATTR(mic_hp_gain, 0664, mic_hp_gain_show, mic_hp_gain_store);
static DEVICE_ATTR(dual_speaker, 0664, dual_speaker_show, dual_speaker_store);
static DEVICE_ATTR(speaker_gain, 0664, speaker_gain_show, speaker_gain_store);
static DEVICE_ATTR(speaker_limits, 0664, speaker_limits_show, NULL);
static DEVICE_ATTR(eq, 0664, eq_show, eq_store);
static DEVICE_ATTR(eq_gains, 0664, eq_gains_show, eq_gains_store);
static DEVICE_ATTR(eq_b1_gain, 0664, eq_b1_gain_show, eq_b1_gain_store);
static DEVICE_ATTR(eq_b2_gain, 0664, eq_b2_gain_show, eq_b2_gain_store);
static DEVICE_ATTR(eq_b3_gain, 0664, eq_b3_gain_show, eq_b3_gain_store);
static DEVICE_ATTR(eq_b4_gain, 0664, eq_b4_gain_show, eq_b4_gain_store);
static DEVICE_ATTR(eq_b5_gain, 0664, eq_b5_gain_show, eq_b5_gain_store);
static DEVICE_ATTR(reset, 0664, reset_show, reset_store);
static DEVICE_ATTR(version, 0664, version_show, NULL);
static DEVICE_ATTR(reg_dump, 0664, reg_dump_show, NULL);

// define attributes
static struct attribute *moro_sound_attributes[] = {
	&dev_attr_moro_sound.attr,
	&dev_attr_headphone_gain.attr,
	&dev_attr_headphone_mono.attr,
	&dev_attr_headphone_limits.attr,
	&dev_attr_earpiece_gain.attr,
	&dev_attr_earpiece_limits.attr,
	&dev_attr_mic.attr,
	&dev_attr_mic_down_gain.attr,
	&dev_attr_mic_up_gain.attr,
	&dev_attr_mic_hp_gain.attr,
	&dev_attr_dual_speaker.attr,
	&dev_attr_speaker_gain.attr,
	&dev_attr_speaker_limits.attr,
	&dev_attr_eq.attr,
	&dev_attr_eq_gains.attr,
	&dev_attr_eq_b1_gain.attr,
	&dev_attr_eq_b2_gain.attr,
	&dev_attr_eq_b3_gain.attr,
	&dev_attr_eq_b4_gain.attr,
	&dev_attr_eq_b5_gain.attr,
	&dev_attr_reset.attr,
	&dev_attr_version.attr,
	&dev_attr_reg_dump.attr,
	NULL
};

// define attribute group
static struct attribute_group moro_sound_control_group = {
	.attrs = moro_sound_attributes,
};


// define control device
static struct miscdevice moro_sound_control_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "moro_sound",
};


/*****************************************/
// Driver init and exit functions
/*****************************************/

static int moro_sound_init(void)
{
	// register moro sound control device
	misc_register(&moro_sound_control_device);
	
	if (sysfs_create_group(&moro_sound_control_device.this_device->kobj,
				&moro_sound_control_group) < 0) {
		pr_err("Moro-sound: failed to create sys fs object.\n");
		return 0;
	}

	// Initialize moro sound master switch with OFF per default (will be set to correct
	// default value when we receive the codec pointer later - avoids startup boot loop)
	moro_sound = 0;

	// Initialize variables
	reset_moro_sound();
	
	pr_info("Moro-sound: engine version %s started\n", MORO_SOUND_VERSION);

	return 0;
}


static void moro_sound_exit(void)
{
	// remove moro sound control device
	sysfs_remove_group(&moro_sound_control_device.this_device->kobj,
                           &moro_sound_control_group);

}


/* define driver entry points */

module_init(moro_sound_init);
module_exit(moro_sound_exit);


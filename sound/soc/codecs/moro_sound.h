/*
 * moro_sound.h  --  Sound mod for Moon, S7 sound driver
 *
 * Author	: @morogoku https://github.com/morogoku
 *
 */


#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <sound/soc.h>

#include <linux/mfd/arizona/registers.h>


/*****************************************/
// External function declarations
/*****************************************/

void moro_sound_hook_moon_pcm_probe(struct regmap *pmap);
int _regmap_write_nohook(struct regmap *map, unsigned int reg, unsigned int val);
int set_speaker_gain(int gain);
int get_speaker_gain(void);


/*****************************************/
// Definitions
/*****************************************/

// Moro sound general
#define MORO_SOUND_DEFAULT 		0
#define MORO_SOUND_VERSION 		"2.2.0"

// headphone levels
#define HEADPHONE_DEFAULT		113
#define HEADPHONE_MIN 			60
#define HEADPHONE_MAX 			190
#define HEADPHONE_MONO_DEFAULT		0

// earpiece levels
#define EARPIECE_DEFAULT		128
#define EARPIECE_MIN 			60
#define EARPIECE_MAX 			190

// dual speaker
#define DUAL_SPEAKER_DEFAULT		0

// speaker levels
#define SPEAKER_DEFAULT			30
#define SPEAKER_MIN 			0
#define SPEAKER_MAX 			63

// Mixers sources
#define OUT1L_MIX_DEFAULT		32
#define OUT1R_MIX_DEFAULT		33
#define EQ1_MIX_DEFAULT			0
#define EQ2_MIX_DEFAULT			0

// EQ gain
#define EQ_DEFAULT			0
#define EQ_GAIN_DEFAULT 		0
#define EQ_GAIN_OFFSET 			12
#define EQ_GAIN_MIN 			-12
#define EQ_GAIN_MAX  			12
#define EQ_B1_GAIN_DEFAULT		0
#define EQ_B2_GAIN_DEFAULT		0
#define EQ_B3_GAIN_DEFAULT		0
#define EQ_B4_GAIN_DEFAULT		0
#define EQ_B5_GAIN_DEFAULT		0


// Mixers
#define ARIZONA_MIXER_SOURCE_MASK	0xff
#define ARIZONA_MIXER_SOURCE_SHIFT	0
#define ARIZONA_MIXER_VOLUME_MASK	0xfe
#define ARIZONA_MIXER_VOLUME_SHIFT	1

// Mic
#define MIC_DEFAULT			0
#define MIC_DOWN_GAIN_DEFAULT		128
#define MIC_UP_GAIN_DEFAULT		128
#define MIC_HP_GAIN_DEFAULT		128


// REGS FOR GET AND SET
// Headphone
#define OUT1L_VOLUME \
	ARIZONA_DAC_DIGITAL_VOLUME_1L, \
	ARIZONA_OUT1L_VOL_MASK, \
	ARIZONA_OUT1L_VOL_SHIFT

#define OUT1R_VOLUME \
	ARIZONA_DAC_DIGITAL_VOLUME_1R, \
	ARIZONA_OUT1R_VOL_MASK, \
	ARIZONA_OUT1R_VOL_SHIFT

#define OUT1_MONO \
	ARIZONA_OUTPUT_PATH_CONFIG_1L, \
	ARIZONA_OUT1_MONO_MASK, \
	ARIZONA_OUT1_MONO_SHIFT

#define OUT1L_MIX \
	ARIZONA_OUT1LMIX_INPUT_1_SOURCE, \
	ARIZONA_MIXER_SOURCE_MASK, \
	ARIZONA_MIXER_SOURCE_SHIFT

#define OUT1R_MIX \
	ARIZONA_OUT1RMIX_INPUT_1_SOURCE, \
	ARIZONA_MIXER_SOURCE_MASK, \
	ARIZONA_MIXER_SOURCE_SHIFT

// Earpiece
#define OUT3L_ENA \
	ARIZONA_OUTPUT_ENABLES_1, \
	ARIZONA_OUT3L_ENA_MASK, \
	ARIZONA_OUT3L_ENA_SHIFT

#define OUT3R_ENA \
	ARIZONA_OUTPUT_ENABLES_1, \
	ARIZONA_OUT3R_ENA_MASK, \
	ARIZONA_OUT3R_ENA_SHIFT	

#define OUT3L_VOLUME \
	ARIZONA_DAC_DIGITAL_VOLUME_3L, \
	ARIZONA_OUT3L_VOL_MASK, \
	ARIZONA_OUT3L_VOL_SHIFT

#define OUT3L_MIX \
	ARIZONA_OUT3LMIX_INPUT_1_SOURCE, \
	ARIZONA_MIXER_SOURCE_MASK, \
	ARIZONA_MIXER_SOURCE_SHIFT

#define OUT3R_MIX \
	ARIZONA_OUT3RMIX_INPUT_1_SOURCE, \
	ARIZONA_MIXER_SOURCE_MASK, \
	ARIZONA_MIXER_SOURCE_SHIFT

// Eq
#define EQ1_ENA \
	ARIZONA_EQ1_1, \
	ARIZONA_EQ1_ENA_MASK, \
	ARIZONA_EQ1_ENA_SHIFT

#define EQ2_ENA \
	ARIZONA_EQ2_1, \
	ARIZONA_EQ2_ENA_MASK, \
	ARIZONA_EQ2_ENA_SHIFT

#define EQ1_MIX \
	ARIZONA_EQ1MIX_INPUT_1_SOURCE, \
	ARIZONA_MIXER_SOURCE_MASK, \
	ARIZONA_MIXER_SOURCE_SHIFT

#define EQ2_MIX \
	ARIZONA_EQ2MIX_INPUT_1_SOURCE, \
	ARIZONA_MIXER_SOURCE_MASK, \
	ARIZONA_MIXER_SOURCE_SHIFT

// Mic
#define MIC1R_VOLUME \
	ARIZONA_ADC_DIGITAL_VOLUME_1R, \
	ARIZONA_IN1R_DIG_VOL_MASK, \
	ARIZONA_IN1R_DIG_VOL_SHIFT

#define MIC3L_VOLUME \
	ARIZONA_ADC_DIGITAL_VOLUME_3L, \
	ARIZONA_IN3L_DIG_VOL_MASK, \
	ARIZONA_IN3L_DIG_VOL_SHIFT

#define MIC2L_VOLUME \
	ARIZONA_ADC_DIGITAL_VOLUME_2L, \
	ARIZONA_IN2L_DIG_VOL_MASK, \
	ARIZONA_IN2L_DIG_VOL_SHIFT

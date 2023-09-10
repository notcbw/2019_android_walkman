/* i.MX AK4458 audio support
 *
 * Copyright 2017 NXP
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <linux/workqueue.h>

#include <linux/extcon.h>
#include <sound/cxd3778gf.h>
#include <sound/lif-md6000-rme.h>
#include <linux/wakelock.h>

#include "fsl_sai.h"
#include "fsl_rpmsg_i2s.h"

/* temporary */
#include <sound/lifmd6000.h>
#include <sound/jack.h>

#ifdef CONFIG_EXTCON
struct extcon_dev *rpmsg_edev;
static const unsigned int imx_rpmsg_extcon_cables[] = {
        EXTCON_JACK_LINE_OUT,
        EXTCON_NONE,
};
#endif

#define CXD3778_UNMUTE_PERIOD_MS  10

static struct snd_soc_jack headphone_jack;

static struct wake_lock rpmsg_cxd3778gf_lock_icx, rpmsg_cxd3778gf_lock_std;

struct imx_cxd3778gf_data {
	struct snd_soc_card card;
	unsigned long freq;

	unsigned int format;
	struct workqueue_struct *imx_cxd3778gf_wq;
	struct work_struct mute_on_work;
	struct delayed_work mute_off_work;
};

static struct imx_cxd3778gf_data *imx_cxd3778gf_drvdata;

static struct snd_soc_dapm_widget imx_cxd3778gf_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Line Out", NULL),
};

static const struct imx_cxd3778gf_fs_mul {
	unsigned int min;
	unsigned int max;
	unsigned int mul;
} fs_mul[] = {
	/**
	 * Table 7      - mapping multiplier and speed mode
	 * Tables 8 & 9 - mapping speed mode and LRCK fs
	 */
	{ .min = 8000,   .max = 32000,  .mul = 1024 }, /* Normal, <= 32kHz */
	{ .min = 44100,  .max = 48000,  .mul = 512  }, /* Normal */
	{ .min = 88200,  .max = 96000,  .mul = 256  }, /* Double */
	{ .min = 176400, .max = 192000, .mul = 128  }, /* Quad */
	{ .min = 352800, .max = 384000, .mul = 2*64 }, /* Oct */
	{ .min = 705600, .max = 768000, .mul = 2*32 }, /* Hex */
};

static bool imx_cxd3778gf_is_dsd(struct snd_pcm_hw_params *params)
{
	snd_pcm_format_t format = params_format(params);

	switch (format) {
	case SNDRV_PCM_FORMAT_DSD_U8:
	case SNDRV_PCM_FORMAT_DSD_U16_LE:
	case SNDRV_PCM_FORMAT_DSD_U16_BE:
	case SNDRV_PCM_FORMAT_DSD_U32_LE:
	case SNDRV_PCM_FORMAT_DSD_U32_BE:
		return true;
	default:
		return false;
	}
}

static void mute_on_work_func(struct work_struct *work)
{
	md6000_mute(1);

	return 0;
}

static void mute_off_work_func(struct work_struct *work)
{
	md6000_mute(0);

	return 0;
}

int imx_cxd3778gf_mute_on_trigger(void)
{
	queue_work(imx_cxd3778gf_drvdata->imx_cxd3778gf_wq,
		   &imx_cxd3778gf_drvdata->mute_on_work);

	return 0;
}
EXPORT_SYMBOL(imx_cxd3778gf_mute_on_trigger);

static unsigned long imx_cxd3778gf_compute_freq(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct imx_cxd3778gf_data *priv = snd_soc_card_get_drvdata(rtd->card);
	unsigned int rate = params_rate(params);
	int i;

	if (imx_cxd3778gf_is_dsd(params))
		return priv->freq;

	/* Find the appropriate MCLK freq */
	for (i = 0; i < ARRAY_SIZE(fs_mul); i++) {
		if (rate >= fs_mul[i].min && rate <= fs_mul[i].max)
			return params_rate(params) * fs_mul[i].mul;
	}

	/* Return default MCLK frequency */
	return priv->freq;
}

static int imx_aif_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_card *card = rtd->card;
	struct imx_cxd3778gf_data *ddata = snd_soc_card_get_drvdata(card);
	struct device *dev = card->dev;
	unsigned int channels = params_channels(params);
	unsigned int fmt = SND_SOC_DAIFMT_NB_NF;
	unsigned int rate = params_rate(params);
	unsigned long freq = imx_cxd3778gf_compute_freq(substream, params);
	bool is_dsd = imx_cxd3778gf_is_dsd(params);
	int ret;

	if (is_dsd) {
		ddata->format = SND_SOC_DAIFMT_PDM;
		fmt |= SND_SOC_DAIFMT_PDM;

		ret = snd_soc_dai_set_fmt(cpu_dai, (fmt | SND_SOC_DAIFMT_CBM_CFS));
		if (ret) {
			dev_err(dev, "failed to set cpu dai fmt: %d\n", ret);
			return ret;
		}

		ret = snd_soc_dai_set_fmt(codec_dai, (fmt | SND_SOC_DAIFMT_CBS_CFS));
		if (ret) {
			dev_err(dev, "failed to set codec dai fmt: %d\n", ret);
			return ret;
		}
	} else {
		ddata->format = SND_SOC_DAIFMT_I2S;
		fmt |= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBM_CFM;

		ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
		if (ret) {
			dev_err(dev, "failed to set cpu dai fmt: %d\n", ret);
			return ret;
		}

		ret = snd_soc_dai_set_fmt(codec_dai, fmt);
		if (ret) {
			dev_err(dev, "failed to set codec dai fmt: %d\n", ret);
			return ret;
		}
	}

	md6000_setup_params(fmt, rate);
	lifmd6000_set_lock(1);

	if (is_dsd)
		ret = snd_soc_dai_set_tdm_slot(cpu_dai,
				0x1, 0x1,
				1, params_width(params));
	else
		ret = snd_soc_dai_set_tdm_slot(cpu_dai,
				BIT(channels) - 1, BIT(channels) - 1,
				2, 32);
	if (ret) {
		dev_err(dev, "failed to set cpu dai tdm slot: %d\n", ret);
		return ret;
	}

	return ret;
}

static int imx_aif_std_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_card *card = rtd->card;
	struct device *dev = card->dev;
	unsigned int channels = params_channels(params);
	unsigned int fmt = SND_SOC_DAIFMT_NB_NF
			 | SND_SOC_DAIFMT_I2S
			 | SND_SOC_DAIFMT_CBM_CFM;
	int ret;

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret) {
		dev_err(dev, "failed to set cpu dai fmt: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret) {
		dev_err(dev, "failed to set codec dai fmt: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(cpu_dai,
				BIT(channels) - 1, BIT(channels) - 1,
				2, 32);
	if (ret) {
		dev_err(dev, "failed to set cpu dai tdm slot: %d\n", ret);
		return ret;
	}

	return ret;
}

static int imx_aif_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int fmt = SND_SOC_DAIFMT_NB_NF
			 | SND_SOC_DAIFMT_I2S
			 | SND_SOC_DAIFMT_CBM_CFM;
	int ret;

	/* Change cxd3778gf to slave mode and dsd mode */
	cxd3778gf_set_icx_hw_free();

	/* Change cpu_dai to slave mode */
	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret) {
		pr_err("%s(%) failed to set cpu dai fmt\n", __func__);
		return ret;
	}

	md6000_setup_free();
	lifmd6000_set_lock(0);

	return 0;
}

static int low_imx_aif_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int fmt = SND_SOC_DAIFMT_NB_NF
			 | SND_SOC_DAIFMT_I2S
			 | SND_SOC_DAIFMT_CBM_CFM;
	int ret;

	/* Change cxd3778gf to slave mode and dsd mode */
	cxd3778gf_set_icx_hw_free();

	/* Change cpu_dai to slave mode */
	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret) {
		pr_err("%s(%) failed to set cpu dai fmt\n", __func__);
		return ret;
	}

	md6000_setup_free();
	lifmd6000_set_lock(0);

	return 0;
}

static int imx_aif_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct imx_cxd3778gf_data *ddata = snd_soc_card_get_drvdata(card);

	switch (ddata->format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_PDM:  /* DSD */
		if (cmd == SNDRV_PCM_TRIGGER_START) {
			queue_delayed_work(ddata->imx_cxd3778gf_wq,
				&ddata->mute_off_work,
				msecs_to_jiffies(CXD3778_UNMUTE_PERIOD_MS));
		}
		break;
	default:
		break;
	}

	return 0;
}

static int imx_aif_hw_master_params(struct snd_pcm_substream *substream,
                                struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_card *card = rtd->card;
	struct device *dev = card->dev;
	unsigned int channels = params_channels(params);
	unsigned int fmt = SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS | SND_SOC_DAIFMT_I2S;
	unsigned long freq = imx_cxd3778gf_compute_freq(substream, params);
	int ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, FSL_SAI_CLK_MAST1, freq,
			SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(dev, "failed to set cpu dai mclk1 rate(%lu): %d\n",
			freq, ret);
		return ret;
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret) {
		dev_err(dev, "failed to set cpu dai fmt: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret) {
		dev_err(dev, "failed to set codec dai fmt: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(cpu_dai,
					BIT(channels) - 1, BIT(channels) - 1,
					2, params_physical_width(params));
	if (ret) {
		dev_err(dev, "failed to set cpu dai tdm slot: %d\n", ret);
		return ret;
	}

	return ret;
}

static int low_imx_aif_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
        struct snd_soc_pcm_runtime *rtd = substream->private_data;
        struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
        struct snd_soc_dai *codec_dai = rtd->codec_dai;
        struct snd_soc_card *card = rtd->card;
        struct device *dev = card->dev;
        unsigned int channels = params_channels(params);
        unsigned int fmt = SND_SOC_DAIFMT_NB_NF;
        unsigned int rate = params_rate(params);
        unsigned long freq = imx_cxd3778gf_compute_freq(substream, params);
        bool is_dsd = imx_cxd3778gf_is_dsd(params);
        int ret;

        if (is_dsd) {
                fmt |= SND_SOC_DAIFMT_PDM;

                ret = snd_soc_dai_set_fmt(cpu_dai, (fmt | SND_SOC_DAIFMT_CBM_CFS));
                if (ret) {
                        dev_err(dev, "failed to set cpu dai fmt: %d\n", ret);
                        return ret;
                }

                ret = snd_soc_dai_set_fmt(codec_dai, (fmt | SND_SOC_DAIFMT_CBS_CFS));
                if (ret) {
                        dev_err(dev, "failed to set codec dai fmt: %d\n", ret);
                        return ret;
                }
        } else {
                fmt |= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBM_CFM;

                ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
                if (ret) {
                        dev_err(dev, "failed to set cpu dai fmt: %d\n", ret);
                        return ret;
                }

                ret = snd_soc_dai_set_fmt(codec_dai, fmt);
                if (ret) {
                        dev_err(dev, "failed to set codec dai fmt: %d\n", ret);
                        return ret;
                }
        }

	md6000_setup_params(fmt, rate);
	lifmd6000_set_lock(1);

        return ret;
}

static const u32 support_rates[] = {
	11025, 22050, 44100, 48000,
	88200, 96000, 176400, 192000, 352800, 384000,
	2822400,
};

static int imx_aif_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = 0;
	static struct snd_pcm_hw_constraint_list constraint_rates;

	constraint_rates.list = support_rates;
	constraint_rates.count = ARRAY_SIZE(support_rates);

	wake_lock(&rpmsg_cxd3778gf_lock_icx);

	ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
						&constraint_rates);
	if (ret)
		return ret;

	return ret;
}

static int imx_aif_std_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = 0;
	static struct snd_pcm_hw_constraint_list constraint_rates;

	constraint_rates.list = support_rates;
	constraint_rates.count = ARRAY_SIZE(support_rates);

	wake_lock(&rpmsg_cxd3778gf_lock_std);

	ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
							&constraint_rates);
	if (ret)
		return ret;

	return ret;
}


static int low_imx_aif_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = 0;
	static struct snd_pcm_hw_constraint_list constraint_rates;

	constraint_rates.list = support_rates;
	constraint_rates.count = ARRAY_SIZE(support_rates);

	ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
                                                &constraint_rates);
	if (ret)
		return ret;

	return ret;
}


static int imx_aif_shutdown(struct snd_pcm_substream *substream)
{
	wake_unlock(&rpmsg_cxd3778gf_lock_icx);
	return 0;
}

static int imx_aif_std_shutdown(struct snd_pcm_substream *substream)
{
	wake_unlock(&rpmsg_cxd3778gf_lock_std);
	return 0;
}

static int imx_dummy_jack_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct device *dev = card->dev;
	int err = 0;

	/* this is for CTS on GSI */
	/* Dummy headphone Jack detection */
	err = snd_soc_card_jack_new(card, "Headphone", SND_JACK_HEADPHONE,
					&headphone_jack,  NULL, 0);
	if (err)
		dev_err(dev, "snd_soc_card_jack_new failed (%d)\n", err);

	/* default insert headphone */
	snd_soc_jack_report(&headphone_jack, SND_JACK_HEADPHONE,
			SND_JACK_HEADPHONE);

	return err;
}

static struct snd_soc_ops imx_aif_ops = {
	.startup   = imx_aif_startup,
	.shutdown  = imx_aif_shutdown,
	.hw_params = imx_aif_hw_params,
	.hw_free = imx_aif_hw_free,
	.trigger = imx_aif_trigger,
};

static struct snd_soc_ops imx_aif_std_ops = {
	.startup   = imx_aif_std_startup,
	.shutdown  = imx_aif_std_shutdown,
	.hw_params = imx_aif_std_hw_params,
};

static struct snd_soc_ops imx_aif_master_ops = {
        .startup   = imx_aif_startup,
        .hw_params = imx_aif_hw_master_params,
};

static struct snd_soc_ops low_imx_aif_ops = {
	.startup   = low_imx_aif_startup,
	.hw_params = low_imx_aif_hw_params,
	.hw_free = low_imx_aif_hw_free,
};

static struct snd_soc_dai_link imx_cxd3778gf_dai[3] = {
	{
	.name = "CXD3778GF_ICX",
	.stream_name = "cxd3778gf-hires-out",
	.codec_dai_name = CXD3778GF_ICX_DAI_NAME,
	.ops = &imx_aif_ops,
	.playback_only = 1,
	.init = imx_dummy_jack_init,
	},
	{
	.name = "CXD3778GF_STD",
	.stream_name = "cxd3778gf-standard",
	.codec_dai_name = CXD3778GF_STD_DAI_NAME,
	.ops = &imx_aif_std_ops,
	},
	{
	.name = "CXD3778GF_ICX_LOW_POWER",
	.stream_name = "cxd3778gf-hires-out_low_power",
	.codec_dai_name = CXD3778GF_ICX_DAI_NAME,
	.ops = &low_imx_aif_ops,
	.playback_only = 1,
	},
#if 0
	{
	.name = "CXD3778GF_dsdenc",
	.stream_name = "dsdenc",
	.codec_dai_name = CXD3778GF_ICX_DAI_NAME,
	.ops = &imx_aif_ops,
     },
	{
	.name = "CXD3778GF_ICX_DSD",
	.stream_name = "cxd3778gf-dsd-out",
	.codec_dai_name = CXD3778GF_ICX_DAI_NAME,
	.ops = &imx_aif_ops,
     },
     {
	.name = "CXD3778GF_ICX_lowpower",
	.stream_name = "cxd3778gf-icx-lowpower",
	.codec_dai_name = CXD3778GF_ICX_DAI_NAME,
	.ops = &imx_aif_ops,
	},
#endif
};

static int imx_cxd3778gf_probe(struct platform_device *pdev)
{
	struct imx_cxd3778gf_data *priv;
	struct device_node *cpu_np_sdin1, *cpu_np_sdin2, *cpu_np_sdin1_low, *codec_np = NULL;
	struct platform_device *cpu_pdev_sdin1, *cpu_pdev_sdin2, *cpu_pdev_sdin1_low;
	struct clk *mclk;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	cpu_np_sdin1 = of_parse_phandle(pdev->dev.of_node, "audio-cpu", 0);
	if (!cpu_np_sdin1) {
		dev_err(&pdev->dev, "audio dai sdin1 phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	cpu_np_sdin2 = of_parse_phandle(pdev->dev.of_node, "audio-cpu", 1);
	if (!cpu_np_sdin2) {
		dev_err(&pdev->dev, "audio dai sdin2 phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	cpu_np_sdin1_low = of_parse_phandle(pdev->dev.of_node, "audio-cpu", 2);
	if (!cpu_np_sdin1_low) {
		dev_err(&pdev->dev, "audio dai sdin1_low phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	codec_np = of_parse_phandle(pdev->dev.of_node, "audio-codec", 0);
	if (!codec_np) {
		dev_err(&pdev->dev, "audio codec phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	cpu_pdev_sdin1 = of_find_device_by_node(cpu_np_sdin1);
	if (!cpu_pdev_sdin1) {
		dev_err(&pdev->dev, "failed to find sdin1 SAI platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	cpu_pdev_sdin2 = of_find_device_by_node(cpu_np_sdin2);
	if (!cpu_pdev_sdin2) {
		dev_err(&pdev->dev, "failed to find sdin2 SAI platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	cpu_pdev_sdin1_low = of_find_device_by_node(cpu_np_sdin1_low);
	if (!cpu_pdev_sdin1_low) {
		dev_err(&pdev->dev, "failed to find sdin1_low SAI platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	imx_cxd3778gf_dai[0].codec_of_node = codec_np;
	imx_cxd3778gf_dai[0].cpu_dai_name = dev_name(&cpu_pdev_sdin1->dev);
	imx_cxd3778gf_dai[0].platform_of_node = cpu_np_sdin1;
	imx_cxd3778gf_dai[0].playback_only = 1;

	imx_cxd3778gf_dai[1].codec_of_node = codec_np;
	imx_cxd3778gf_dai[1].cpu_dai_name = dev_name(&cpu_pdev_sdin2->dev);
	imx_cxd3778gf_dai[1].platform_of_node = cpu_np_sdin2;

	imx_cxd3778gf_dai[2].codec_of_node = codec_np;
	imx_cxd3778gf_dai[2].cpu_dai_name = dev_name(&cpu_pdev_sdin1_low->dev);
	imx_cxd3778gf_dai[2].platform_of_node = cpu_np_sdin1_low;

	priv->card.dai_link = imx_cxd3778gf_dai;
	priv->card.num_links = 3;
	priv->card.dev = &pdev->dev;
	priv->card.owner = THIS_MODULE;
	priv->card.dapm_widgets = imx_cxd3778gf_dapm_widgets;
	priv->card.num_dapm_widgets = ARRAY_SIZE(imx_cxd3778gf_dapm_widgets);

	priv->imx_cxd3778gf_wq = create_singlethread_workqueue("imx_cxd3778gf_wq");
	if (!priv->imx_cxd3778gf_wq) {
		dev_err(&pdev->dev, "%s(): failed to create wq\n", __func__);
		return -ENOMEM;
	}

	INIT_WORK(&priv->mute_on_work, mute_on_work_func);
	INIT_DELAYED_WORK(&priv->mute_off_work, mute_off_work_func);

	ret = snd_soc_of_parse_card_name(&priv->card, "model");
	if (ret)
		goto fail;

	snd_soc_card_set_drvdata(&priv->card, priv);

	imx_cxd3778gf_drvdata = priv;

	ret = devm_snd_soc_register_card(&pdev->dev, &priv->card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		goto fail;
	}

	ret = 0;

#ifdef CONFIG_EXTCON
	rpmsg_edev  = devm_extcon_dev_allocate(&pdev->dev, imx_rpmsg_extcon_cables);
	if (IS_ERR(rpmsg_edev)) {
		dev_err(&pdev->dev, "failed to allocate extcon device\n");
		goto fail;
	}

	ret = devm_extcon_dev_register(&pdev->dev,rpmsg_edev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register extcon device\n");
		goto fail;
	}
	extcon_set_state_sync(rpmsg_edev, EXTCON_JACK_LINE_OUT, 1);
#endif

	wake_lock_init(&rpmsg_cxd3778gf_lock_icx, WAKE_LOCK_SUSPEND, "lpa_cxd3778gf_rpmsg_icx");
	wake_lock_init(&rpmsg_cxd3778gf_lock_std, WAKE_LOCK_SUSPEND, "lpa_cxd3778gf_rpmsg_std");

fail:
	if (cpu_np_sdin1)
		of_node_put(cpu_np_sdin1);
	if (cpu_np_sdin2)
		of_node_put(cpu_np_sdin2);
	if (codec_np)
		of_node_put(codec_np);

	return ret;
}

static int imx_cxd3778gf_remove(struct platform_device *pdev)
{
	cancel_work_sync(&imx_cxd3778gf_drvdata->mute_on_work);
	cancel_delayed_work_sync(&imx_cxd3778gf_drvdata->mute_off_work);
	destroy_workqueue(imx_cxd3778gf_drvdata->imx_cxd3778gf_wq);

	return 0;
}

static int imx_cxd3778gf_shutdown(struct platform_device *pdev)
{
	cancel_work_sync(&imx_cxd3778gf_drvdata->mute_on_work);
	cancel_delayed_work_sync(&imx_cxd3778gf_drvdata->mute_off_work);
	destroy_workqueue(imx_cxd3778gf_drvdata->imx_cxd3778gf_wq);

	return 0;
}

static const struct of_device_id imx_cxd3778gf_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-cxd3778gf", },
	{ },
};
MODULE_DEVICE_TABLE(of, imx_cxd3778gf_dt_ids);

static struct platform_driver imx_cxd3778gf_driver = {
	.driver = {
		.name = "imx-cxd3778gf",
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_cxd3778gf_dt_ids,
	},
	.probe = imx_cxd3778gf_probe,
	.remove = imx_cxd3778gf_remove,
	.shutdown = imx_cxd3778gf_shutdown,
};
module_platform_driver(imx_cxd3778gf_driver);

MODULE_DESCRIPTION("Freescale i.MX CXD3778GF ASoC machine driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-cxd3778gf");

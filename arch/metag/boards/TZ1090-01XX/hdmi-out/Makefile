obj-${CONFIG_TZ1090_01XX_HDMI_VID_OUT}	+= setup.o sii9022a.o

ifeq (${CONFIG_SND_TZ1090_SOC},y)
# ALSA output
snd-soc-sii9022a-objs			:= sii9022a-audio.o
obj-${CONFIG_TZ1090_01XX_HDMI_AUDIO}	+= snd-soc-sii9022a.o
endif


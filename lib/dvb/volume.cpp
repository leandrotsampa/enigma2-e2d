#include <lib/base/cfile.h>
#include <lib/base/eerror.h>
#include <lib/dvb/volume.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/dvb/version.h>
#if DVB_API_VERSION < 3
#define VIDEO_DEV "/dev/dvb/card0/video0"
#define AUDIO_DEV "/dev/dvb/card0/audio0"
#include <ost/audio.h>
#include <ost/video.h>
#else
#ifdef HAVE_HISIAPI
#define VIDEO_DEV "/dev/player/video0"
#define AUDIO_DEV "/dev/player/audio0"
#else
#define VIDEO_DEV "/dev/dvb/adapter0/video0"
#define AUDIO_DEV "/dev/dvb/adapter0/audio0"
#endif
#include <linux/dvb/audio.h>
#include <linux/dvb/video.h>
#endif

#ifdef HAVE_ALSA
# ifndef ALSA_VOLUME_MIXER
#  define ALSA_VOLUME_MIXER "Master"
# endif
# ifndef ALSA_CARD
#  define ALSA_CARD "default"
# endif
#endif

eDVBVolumecontrol* eDVBVolumecontrol::instance = NULL;

eDVBVolumecontrol* eDVBVolumecontrol::getInstance()
{
	if (instance == NULL)
		instance = new eDVBVolumecontrol;
	return instance;
}

eDVBVolumecontrol::eDVBVolumecontrol()
:m_volsteps(5)
{
#ifdef HAVE_ALSA
	mainVolume = NULL;
	openMixer();
#endif
	volumeUnMute();
#if not defined (__sh__) // dont reset volume on start
	setVolume(100, 100);
#endif
}

int eDVBVolumecontrol::openMixer()
{
#ifdef HAVE_ALSA
	if (!mainVolume)
	{
		int err;
		char *card = ALSA_CARD;

		eDebug("[eDVBVolumecontrol] Setup ALSA Mixer %s - %s", ALSA_CARD, ALSA_VOLUME_MIXER);
		/* Perform the necessary pre-amble to start up ALSA Mixer */
		err = snd_mixer_open(&alsaMixerHandle, 0);
		if (err < 0)
		{
			eDebug("[eDVBVolumecontrol] Mixer %s open error: %s", card, snd_strerror(err));
			return err;
		}
		err = snd_mixer_attach(alsaMixerHandle, card);
		if (err < 0)
		{
			eDebug("[eDVBVolumecontrol] Mixer attach %s error: %s", card, snd_strerror(err));
			snd_mixer_close(alsaMixerHandle);
			alsaMixerHandle = NULL;
			return err;
		}
		err = snd_mixer_selem_register(alsaMixerHandle, NULL, NULL);
		if (err < 0)
		{
			eDebug("[eDVBVolumecontrol] Mixer register error: %s", snd_strerror(err));
			snd_mixer_close(alsaMixerHandle);
			alsaMixerHandle = NULL;
			return err;
		}
		err = snd_mixer_load(alsaMixerHandle);
		if (err < 0)
		{
			eDebug("[eDVBVolumecontrol] Mixer %s load error: %s", card, snd_strerror(err));
			snd_mixer_close(alsaMixerHandle);
			alsaMixerHandle = NULL;
			return err;
		}

		/* Set up Decoder 0 as the main volume control. */
		snd_mixer_selem_id_t *sid;
		snd_mixer_selem_id_alloca(&sid);
		snd_mixer_selem_id_set_name(sid, ALSA_VOLUME_MIXER);
		snd_mixer_selem_id_set_index(sid, 0);
		mainVolume = snd_mixer_find_selem(alsaMixerHandle, sid);
	}
	return mainVolume ? 0 : -1;
#else
	return open( AUDIO_DEV, O_RDWR );
#endif
}

void eDVBVolumecontrol::closeMixer(int fd)
{
#ifdef HAVE_ALSA
	/* we want to keep the alsa mixer */
#else
	if (fd >= 0) close(fd);
#endif
}

void eDVBVolumecontrol::setVolumeSteps(int steps)
{
	m_volsteps = steps;
}

void eDVBVolumecontrol::volumeUp(int left, int right)
{
	setVolume(leftVol + (left ? left : m_volsteps), rightVol + (right ? right : m_volsteps));
}

void eDVBVolumecontrol::volumeDown(int left, int right)
{
	setVolume(leftVol - (left ? left : m_volsteps), rightVol - (right ? right : m_volsteps));
}

int eDVBVolumecontrol::checkVolume(int vol)
{
	if (vol < 0)
		vol = 0;
	else if (vol > 100)
		vol = 100;
	return vol;
}

void eDVBVolumecontrol::setVolume(int left, int right)
{
		/* left, right is 0..100 */
	leftVol = checkVolume(left);
	rightVol = checkVolume(right);

#ifdef HAVE_ALSA
	eDebug("[eDVBVolumecontrol] Setvolume: ALSA leftVol=%d", leftVol);
	if (mainVolume)
#ifdef HAVE_HISIAPI
		snd_mixer_selem_set_playback_volume_all(mainVolume, muted ? 0 : leftVol > 95 ? 95 : leftVol);
#else
		snd_mixer_selem_set_playback_volume_all(mainVolume, muted ? 0 : leftVol);
#endif
#else
		/* convert to -1dB steps */
	left = 63 - leftVol * 63 / 100;
	right = 63 - rightVol * 63 / 100;
		/* now range is 63..0, where 0 is loudest */

	audio_mixer_t mixer;

	mixer.volume_left = left;
	mixer.volume_right = right;

	eDebug("[eDVBVolumecontrol] Setvolume: raw: %d %d, -1db: %d %d", leftVol, rightVol, left, right);

	int fd = openMixer();
	if (fd >= 0)
	{
#ifdef DVB_API_VERSION
		if (ioctl(fd, AUDIO_SET_MIXER, &mixer) < 0) {
			eDebug("[eDVBVolumecontrol] Setvolume failed: %m");
		}
#endif
		closeMixer(fd);
		return;
	}
	else {
		eDebug("[eDVBVolumecontrol] SetVolume failed to open mixer: %m");
	}

	//HACK?
	CFile::writeInt("/proc/stb/avs/0/volume", left); /* in -1dB */
#endif
}

int eDVBVolumecontrol::getVolume()
{
	return leftVol;
}

bool eDVBVolumecontrol::isMuted()
{
	return muted;
}


void eDVBVolumecontrol::volumeMute()
{
#ifdef HAVE_ALSA
	eDebug("[eDVBVolumecontrol] Setvolume: ALSA Mute");
	if (mainVolume)
		snd_mixer_selem_set_playback_volume_all(mainVolume, 0);
	muted = true;
#else
	int fd = openMixer();
	if (fd >= 0)
	{
#ifdef DVB_API_VERSION
		ioctl(fd, AUDIO_SET_MUTE, true);
#endif
		closeMixer(fd);
	}
	muted = true;

	//HACK?
	CFile::writeInt("/proc/stb/audio/j1_mute", 1);
#endif
}

void eDVBVolumecontrol::volumeUnMute()
{
#ifdef HAVE_ALSA
	eDebug("[eDVBVolumecontrol] Setvolume: ALSA unMute to %d", leftVol);
	if (mainVolume)
#ifdef HAVE_HISIAPI
		snd_mixer_selem_set_playback_volume_all(mainVolume, leftVol > 95 ? 95 : leftVol);
#else
		snd_mixer_selem_set_playback_volume_all(mainVolume, leftVol);
#endif
	muted = false;
#else
	int fd = openMixer();
	if (fd >= 0)
	{
#ifdef DVB_API_VERSION
		ioctl(fd, AUDIO_SET_MUTE, false);
#endif
		closeMixer(fd);
	}
	muted = false;

	//HACK?
	CFile::writeInt("/proc/stb/audio/j1_mute", 0);
#endif
}

void eDVBVolumecontrol::volumeToggleMute()
{
	if (isMuted())
		volumeUnMute();
	else
		volumeMute();
}

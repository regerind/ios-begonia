/*
 * begonia: the display's brightness as iOS looks for it. CoreBrightness (CBDisplayContaineriOS) matches an IOService
 * of the class AppleARMBacklight and reads its IODisplayParameters (brightness: min, max, value); without one, iOS has
 * no internal display brightness, the Control Centre slider does not move and backboardd keeps 0.5 (y93). This is that
 * service: its brightness written by iOS goes to the LM36273 (begonia_backlight.c, 0..255), and is published back.
 * The keys iOS writes are said (the first ones), so the rest of what it expects can be learned from a session's log.
 */
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/c++/OSCollectionIterator.h>

extern "C" void begonia_backlight_set(uint32_t level);
extern "C" uint32_t begonia_backlight_get(void);
extern "C" void begonia_perf_note(const char *format, ...);

#define BL_MAX  65536u

class AppleARMBacklight : public IOService {
	OSDeclareDefaultStructors(AppleARMBacklight);
public:
	virtual IOReturn setProperties(OSObject *properties) APPLE_KEXT_OVERRIDE;
	void publish(uint32_t value);
	uint32_t current;
};
OSDefineMetaClassAndStructors(AppleARMBacklight, IOService);

void
AppleARMBacklight::publish(uint32_t value)
{
	OSDictionary *params = OSDictionary::withCapacity(1), *brightness = OSDictionary::withCapacity(3);
	OSNumber *min = OSNumber::withNumber(0ULL, 32), *max = OSNumber::withNumber((unsigned long long)BL_MAX, 32);
	OSNumber *val = OSNumber::withNumber((unsigned long long)value, 32);

	if (params && brightness && min && max && val) {
		brightness->setObject("min", min);
		brightness->setObject("max", max);
		brightness->setObject("value", val);
		params->setObject("brightness", brightness);
		setProperty("IODisplayParameters", params);
	}
	OSSafeReleaseNULL(min);
	OSSafeReleaseNULL(max);
	OSSafeReleaseNULL(val);
	OSSafeReleaseNULL(brightness);
	OSSafeReleaseNULL(params);
	current = value;
}

/* a number from what iOS wrote: a number, or a dictionary with "value" (IODisplayParameters' own shape) */
static bool
number_of(OSObject *o, uint32_t *out)
{
	OSNumber *n = OSDynamicCast(OSNumber, o);
	OSDictionary *d = OSDynamicCast(OSDictionary, o);

	if (n == NULL && d != NULL) {
		n = OSDynamicCast(OSNumber, d->getObject("value"));
	}
	if (n == NULL) {
		return false;
	}
	*out = n->unsigned32BitValue();
	return true;
}

IOReturn
AppleARMBacklight::setProperties(OSObject *properties)
{
	static unsigned said;
	OSDictionary *dict = OSDynamicCast(OSDictionary, properties);
	uint32_t value = 0;
	bool found = false;

	if (dict == NULL) {
		return kIOReturnBadArgument;
	}
	if (said < 12) {
		OSCollectionIterator *it = OSCollectionIterator::withCollection(dict);
		char keys[160] = "";
		size_t used = 0;
		if (it != NULL) {
			OSSymbol *key;
			while ((key = OSDynamicCast(OSSymbol, it->getNextObject())) != NULL && used < sizeof(keys) - 40) {
				used += (size_t)snprintf(keys + used, sizeof(keys) - used, " %s", key->getCStringNoCopy());
			}
			it->release();
		}
		said++;
		IOLog("BEGONIA BACKLIGHT: AppleARMBacklight set:%s\n", keys);
		if (said <= 4) {
			begonia_perf_note("BEGONIA BACKLIGHT: AppleARMBacklight set:%s", keys);
		}
	}
	found = number_of(dict->getObject("brightness"), &value);
	if (!found) {
		OSDictionary *params = OSDynamicCast(OSDictionary, dict->getObject("IODisplayParameters"));
		if (params != NULL) {
			found = number_of(params->getObject("brightness"), &value);
		}
	}
	if (found) {
		if (value > BL_MAX) {
			value = BL_MAX;
		}
		uint32_t level = (uint32_t)(((uint64_t)value * 255 + BL_MAX / 2) / BL_MAX);
		begonia_backlight_set(level < 4 ? 4 : level);
		publish(value);
	}
	return kIOReturnSuccess;
}

extern "C" void
BegoniaBacklightStart(void)
{
	IOService *platform = IOService::getPlatform();
	AppleARMBacklight *bl = new AppleARMBacklight;

	if (platform == NULL || bl == NULL || !bl->init()) {
		IOLog("BEGONIA BACKLIGHT: no AppleARMBacklight\n");
		return;
	}
	bl->setProperty("backlight-control", kOSBooleanTrue);
	bl->setProperty("built-in", kOSBooleanTrue);
	bl->publish((begonia_backlight_get() * BL_MAX + 127) / 255);
	if (!bl->attach(platform) || !bl->start(platform)) {
		IOLog("BEGONIA BACKLIGHT: AppleARMBacklight did not start\n");
		return;
	}
	bl->registerService();
	IOLog("BEGONIA BACKLIGHT: AppleARMBacklight published (IODisplayParameters brightness 0..%u)\n", BL_MAX);
}

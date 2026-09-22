#pragma once

#include "jucePluginLib/controller.h"
#include "mdLib/mdautomation.h"
#include "mdLib/mdautomationsync.h"
#include "mdLib/mdtypes.h"
#include "mdRealtimeQueue.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	struct ControllerAutomationTestAccess;

	class Controller : public pluginLib::Controller
	{
	public:
		explicit Controller(AudioPluginAudioProcessor& _p);
		~Controller() override;

		void onStateLoaded() override;

		uint8_t getPartCount() const override;
		md::MachineModel getModel() const { return m_model; }

		bool parseSysexMessage(const pluginLib::SysEx&,
			synthLib::MidiEventSource) override;
		bool parseControllerMessage(const synthLib::SMidiEvent& _event) override;
		bool parseMidiMessage(const synthLib::SMidiEvent& _event) override;
		void processRealtimeParameterChanges(size_t _maximumChanges) override;
		void processOfflineControllerWork() override
		{
			processPendingMidiMessages();
		}

		void sendParameterChange(const pluginLib::Parameter& _parameter,
			pluginLib::ParamValue _value, pluginLib::Parameter::Origin _origin) override;

		bool isAutomationSynchronized() const
		{
			return m_automationReady.load(std::memory_order_acquire);
		}
		uint8_t getAutomationBaseChannel() const
		{
			return m_baseChannel.load(std::memory_order_acquire);
		}
		bool hasAutomationGlobalSnapshot() const
		{
			return m_haveGlobal.load(std::memory_order_acquire);
		}
		bool hasAutomationKitSnapshot() const
		{
			return m_haveKit.load(std::memory_order_acquire);
		}
		uint64_t getTransmittedAutomationChangeCount() const
		{
			return m_transmittedAutomationChanges.load(std::memory_order_acquire);
		}
		uint64_t getTransmittedAutomationDigest() const
		{
			return m_transmittedAutomationDigest.load(std::memory_order_acquire);
		}
		uint64_t getRealtimeAutomationOverflowCount() const
		{
			return m_realtimeAutomationOverflows.load(std::memory_order_acquire);
		}
		uint64_t getSynchronizationRequestCount() const
		{
			return m_synchronizationRequests.load(std::memory_order_acquire);
		}
		int getLastFirmwareKitValue(const pluginLib::Parameter& _parameter) const;
		void requestAutomationState();
		// Pattern dump round trip for editor gestures. The listener is invoked on
		// the controller's protocol thread with device-origin 0x67 dumps.
		void setPatternDumpListener(std::function<void(const std::vector<uint8_t>&)> _listener);
		// Every device-origin SysEx message (tests / diagnostics).
		void setSysexListener(std::function<void(const std::vector<uint8_t>&)> _listener);
		void requestCurrentPatternDump();
		void sendSysexToDevice(const std::vector<uint8_t>& _message) const;
		// Saves the live kit into its own slot. Returns false when the slot is unknown.
		bool saveCurrentKit() const;
		// Appends a timestamped line to <data folder>/logs/randomize.log and stderr.
		void diagnostic(const std::string& _message) const;
		// Per-track exclusion from the randomize gestures, saved with the plugin state.
		enum class RandomizeAspect : uint8_t { Trigs = 0, Machines = 1, Locks = 2 };
		bool isTrackExcluded(const RandomizeAspect _aspect, const uint8_t _track) const
		{
			return _track < 16 && (m_randomizeExclude[static_cast<size_t>(_aspect)].load(std::memory_order_acquire) >> _track & 1u);
		}
		void setTrackExcluded(const RandomizeAspect _aspect, const uint8_t _track, const bool _excluded)
		{
			if(_track >= 16) return;
			auto& mask = m_randomizeExclude[static_cast<size_t>(_aspect)];
			auto value = mask.load(std::memory_order_acquire);
			value = _excluded ? static_cast<uint16_t>(value | 1u << _track) : static_cast<uint16_t>(value & ~(1u << _track));
			mask.store(value, std::memory_order_release);
		}
		uint16_t getExcludeMask(const RandomizeAspect _aspect) const
		{
			return m_randomizeExclude[static_cast<size_t>(_aspect)].load(std::memory_order_acquire);
		}
		void setExcludeMask(const RandomizeAspect _aspect, const uint16_t _mask)
		{
			m_randomizeExclude[static_cast<size_t>(_aspect)].store(_mask, std::memory_order_release);
		}
		// Protection masks for loudness/effect parameters (see mdRandomizeProtect.h);
		// bit n set = entry n protected. Default: everything protected.
		uint32_t getProtectValuesMask() const { return m_protectValuesMask.load(std::memory_order_acquire); }
		uint32_t getProtectLocksMask() const { return m_protectLocksMask.load(std::memory_order_acquire); }
		void setProtectValuesMask(const uint32_t _mask) { m_protectValuesMask.store(_mask, std::memory_order_release); }
		void setProtectLocksMask(const uint32_t _mask) { m_protectLocksMask.store(_mask, std::memory_order_release); }
		// Chance (1..99 %) that a parameter is chosen at all for random locks.
		static constexpr int g_paramChanceDefault = 100;
		int getParamChancePercent() const { return m_paramChancePercent.load(std::memory_order_acquire); }
		void setParamChancePercent(const int _percent)
		{
			m_paramChancePercent.store(std::clamp(_percent, 1, 100), std::memory_order_release);
		}
		// Per-model defaults for a fresh instance (see applyRandomizeDefaults()).
		int defaultTrigChancePercent() const { return m_model == md::MachineModel::Machinedrum ? 35 : g_trigChanceDefault; }
		int defaultParamChancePercent() const { return m_model == md::MachineModel::Machinedrum ? 35 : g_paramChanceDefault; }
		void applyRandomizeDefaults();
		// AFX/AE groove mode: role-based trig grammars instead of uniform chance.
		bool getGrooveMode() const { return m_grooveMode.load(std::memory_order_acquire); }
		void setGrooveMode(const bool _on) { m_grooveMode.store(_on, std::memory_order_release); }
		// Chance (1..99 %) that a trig receives a lock on a randomized parameter; per instance.
		static constexpr int g_lockChanceDefault = 50;
		int getLockChancePercent() const { return m_lockChancePercent.load(std::memory_order_acquire); }
		void setLockChancePercent(const int _percent)
		{
			m_lockChancePercent.store(std::clamp(_percent, 1, 99), std::memory_order_release);
		}
		// Chance (1..99 %) that a step receives a trig; per instance, saved with the state.
		static constexpr int g_trigChanceDefault = 50;
		int getTrigChancePercent() const { return m_trigChancePercent.load(std::memory_order_acquire); }
		void setTrigChancePercent(const int _percent)
		{
			m_trigChancePercent.store(std::clamp(_percent, 1, 99), std::memory_order_release);
		}
		// Kit-dump machine model word for a Machinedrum track, 0xffffffff when
		// no kit dump has been seen yet (see md::scale::tuningForModel).
		uint32_t getTrackModel(const uint8_t _track) const
		{
			return _track < 16 ? m_trackModels[_track].load(std::memory_order_acquire) : 0xffffffffu;
		}
		const ParameterList& findTrackParameters(const uint8_t _track,
			const uint8_t _page, const uint8_t _index) const
		{
			return findSynthParam(_track, _page, _index);
		}
		std::vector<uint8_t> createAutomationSnapshot() const;
		bool restoreAutomationSnapshot(const std::vector<uint8_t>& _snapshot);

	private:
		friend struct ControllerAutomationTestAccess;
		struct Address
		{
			uint8_t page = 0;
			uint8_t track = 0;
			uint8_t index = 0;

			bool operator<(const Address& _other) const
			{
				if(page != _other.page) return page < _other.page;
				if(track != _other.track) return track < _other.track;
				return index < _other.index;
			}
		};

		struct AutomationSlot
		{
			Address address;
			// One atomic publication is the source of truth for this address. The low
			// byte is the value, the middle bits identify the publication, and the top
			// bit means that the value still needs to reach the firmware. Queue entries
			// are only delivery hints and may safely be stale or absent.
			std::atomic<uint64_t> publication{0};
			// Publications older than this revision are intentionally superseded. UI
			// edits and overflow advance the floor; ordinary DAW writes do not, so
			// every queued host value retains its FIFO delivery semantics.
			std::atomic<uint64_t> deliveryFloorRevision{0};
			// Exact publication whose queue hint was dropped. A versioned marker avoids
			// clearing a newer producer's recovery obligation after a concurrent scan.
			std::atomic<uint64_t> scanPublication{0};
			// Raw value from the most recently accepted stored-Kit dump. This is
			// diagnostic truth, distinct from the live/session publication above.
			std::atomic<uint16_t> lastFirmwareKitValue{0x100};
		};

		struct QueuedAutomationChange
		{
			md::automation::ParameterChange change;
			size_t slotIndex = 0;
			uint64_t publication = 0;
		};

		static constexpr size_t RealtimeAutomationCapacity = 4096;
		static constexpr uint64_t PublicationDirty = uint64_t{1} << 63;
		static constexpr uint64_t PublicationValueMask = 0x7f;
		static constexpr uint64_t PublicationRevisionMask =
			~(PublicationDirty | uint64_t{0xff});
		static_assert(std::atomic<uint64_t>::is_always_lock_free,
			"realtime automation requires lock-free 64-bit atomics");

		pluginLib::Parameter* createParameter(pluginLib::Controller& _controller,
			const pluginLib::Description& _description, uint8_t _part, int _uid,
			const pluginLib::Parameter::PartFormatter& _formatter) override;
		void requestKitState();
		void requestAutomationState(bool _forceApplyKitDump);
		void transmitParameterChange(const md::automation::ParameterChange& _change);
		bool transmitRealtimeParameterChange(
			const md::automation::ParameterChange& _change);
		void drainRealtimeParameterChanges(size_t _maximumChanges, bool _realtime);
		bool deliverAutomationPublication(const QueuedAutomationChange& _queued,
			bool _realtime);
		uint64_t createPublication(uint8_t _value, bool _dirty);
		void publishAutomationIntent(const md::automation::ParameterChange& _change,
			bool _supersedeEarlier = false);
		uint8_t publishFirmwareValue(const Address& _address, uint8_t _value,
			uint64_t _kitRequestRevision = 0);
		static uint8_t publicationValue(uint64_t _publication);
		static uint64_t publicationRevision(uint64_t _publication);
		static bool publicationIsDirty(uint64_t _publication);
		AutomationSlot* findAutomationSlot(const Address& _address);
		const AutomationSlot* findAutomationSlot(const Address& _address) const;
		void completeSynchronizationIfReady();
		bool firmwareReadyForAutomation() const;
		void applyKitParameters(const std::vector<md::automation::ParameterChange>& _changes);
		void onControllerTimer() override;
		void sendMissingSynchronizationRequests();
		void sendSynchronizationRequest(const pluginLib::SysEx& _message) const;
		static uint64_t milliseconds();

		const md::MachineModel m_model;
		std::atomic<uint8_t> m_baseChannel{0x7f};
		std::atomic<bool> m_haveGlobal{false};
		std::atomic<bool> m_haveKit{false};
		std::atomic<bool> m_automationReady{false};
		std::atomic<uint64_t> m_lastStatePollMs{0};
		std::atomic<uint64_t> m_kitDumpRequestRevision{0};
		std::atomic<bool> m_forceApplyRequestedKitDump{false};
		std::atomic<bool> m_applyRequestedKitDump{true};
		// The timer/offline consumer is serialized by pluginLib::Controller, but
		// explicit state loads and program changes may request a resync from another
		// non-realtime thread. Keep the protocol trackers and their coupled request
		// flags single-owner across both entry paths. Recursive locking is deliberate:
		// parsing SET STATUS delegates to requestKitState(), and request helpers delegate
		// to sendMissingSynchronizationRequests(). This mutex is never taken by the
		// realtime parameter-publication path.
		std::recursive_mutex m_synchronizationLock;
		md::automation::DumpRequestTracker m_globalSynchronization{true};
		md::automation::DumpRequestTracker m_kitSynchronization{false};
		std::atomic<uint64_t> m_synchronizationEpoch{0};
		std::atomic<uint64_t> m_nextAutomationRevision{1};
		std::atomic<uint64_t> m_transmittedAutomationChanges{0};
		std::atomic<uint64_t> m_transmittedAutomationDigest{14695981039346656037ull};
		std::atomic<uint8_t> m_currentGlobal{0xff};
		std::atomic<uint8_t> m_currentKit{0xff};
		std::deque<AutomationSlot> m_automationSlots;
		std::map<Address, size_t> m_automationSlotIndices;
		RealtimeQueue<QueuedAutomationChange,
			RealtimeAutomationCapacity> m_realtimeAutomationChanges;
		size_t m_dirtyScanPosition = 0;
		bool m_minimumBudgetRecoveryTurn = true;
		std::atomic_flag m_realtimeAutomationDrain = ATOMIC_FLAG_INIT;
		std::atomic<uint64_t> m_realtimeAutomationOverflows{0};
		mutable std::atomic<uint64_t> m_synchronizationRequests{0};
		bool m_syntheticFirmwareReadyForTests = false;
		std::atomic<bool> m_patternDumpPending{false};
		std::array<std::atomic<uint32_t>, 16> m_trackModels;
		std::array<std::atomic<uint16_t>, 3> m_randomizeExclude{};
		std::atomic<int> m_trigChancePercent{g_trigChanceDefault};
		std::atomic<int> m_lockChancePercent{g_lockChanceDefault};
		std::atomic<bool> m_grooveMode{false};
		std::atomic<int> m_paramChancePercent{g_paramChanceDefault};
		uint64_t m_loggedClockRelocates = 0, m_loggedClockRephases = 0, m_lastClockLogMs = 0;
		uint64_t m_loggedHostClocks = 0, m_loggedHostTransport = 0, m_loggedTicks = 0;
		std::atomic<uint32_t> m_protectValuesMask{0xffffffffu};
		std::atomic<uint32_t> m_protectLocksMask{0xffffffffu};
		std::mutex m_patternDumpListenerLock;
		std::function<void(const std::vector<uint8_t>&)> m_patternDumpListener;
		std::function<void(const std::vector<uint8_t>&)> m_sysexListener;
		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Controller)
	};
}

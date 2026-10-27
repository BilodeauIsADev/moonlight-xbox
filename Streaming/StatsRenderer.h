#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include "..\Common\DeviceResources.h"
#include "..\Common\StepTimer.h"
#include "..\State\Stats.h"

namespace moonlight_xbox_dx {
class StatsRenderer
{
  public:
	StatsRenderer(const std::shared_ptr<DX::DeviceResources> &deviceResources);
	void CreateDeviceDependentResources();
	void CreateWindowSizeDependentResources();
	void ReleaseDeviceDependentResources();
	void Update(DX::StepTimer const &timer);
	void Render(bool showGraphs);
	void RenderGraphs();
	bool GetVisible();
	void SetVisible(bool visible);
	void ToggleVisible();

  private:
	struct Tile {
		char value[48];
		char unit[16];
		char detail[128];
		ImU32 color;
	};
	void FormatSnapshot(const StatsSnapshot &snapshot);
	void RenderCard(bool showGraphs);
	void DrawSparkline(int history, ImDrawList *draw, ImVec2 pos, ImVec2 size);

	std::mutex m_mutex;
	std::shared_ptr<DX::DeviceResources> m_deviceResources;
	bool m_visible;
	bool m_hasSnapshot = false;
	uint32_t m_displayWidth = 0;
	uint32_t m_displayHeight = 0;
	Tile m_tiles[8] = {};
	char m_videoMode[128] = {};
	char m_secondary[2][160] = {};
	// Per-second averages, only collected while graph collection is enabled.
	float m_history[2][120] = {};
	int m_historyCount = 0;
	int m_historyHead = 0;
};
} // namespace moonlight_xbox_dx

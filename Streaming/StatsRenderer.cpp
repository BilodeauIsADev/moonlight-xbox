// clang-format off
#include "pch.h"
// clang-format on
#include "StatsRenderer.h"
#include "../Plot/ImGuiPlots.h"
#include "../Plot/PlotDesc.h"
#include "PyroWaveDecoder.h"
#include "Utils.hpp"
#include <cmath>
#include <cstdio>

using namespace moonlight_xbox_dx;

namespace {
const ImU32 White = IM_COL32(245, 245, 247, 255);
const ImU32 Muted = IM_COL32(174, 174, 184, 255);
const ImU32 Green = IM_COL32(61, 220, 132, 255);
const ImU32 Amber = IM_COL32(255, 176, 32, 255);
const ImU32 Red = IM_COL32(255, 77, 79, 255);

ImU32 latencyColor(double value, double warning, double danger) {
	return value > danger ? Red : value > warning ? Amber : Green;
}

const ImGuiWindowFlags OverlayFlags = ImGuiWindowFlags_NoDecoration |
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
}

StatsRenderer::StatsRenderer(const std::shared_ptr<DX::DeviceResources> &deviceResources)
    : m_deviceResources(deviceResources), m_visible(false) {
	CreateDeviceDependentResources();
}

void StatsRenderer::Update(DX::StepTimer const &timer) {
	std::lock_guard<std::mutex> lock(m_mutex);
	// Keep accumulating while hidden. Retain the legacy text API for other users.
	char output[2048];
	if (Stats::instance().ShouldUpdateDisplay(timer, m_visible, output, sizeof(output))) {
		FormatSnapshot(Stats::instance().GetDisplaySnapshot());
	}
}

void StatsRenderer::FormatSnapshot(const StatsSnapshot &s) {
	m_hasSnapshot = s.valid;
	if (!s.valid) return;
	const VIDEO_STATS &v = s.video;
	const double decode = v.decodedFrames ? v.totalDecodeTimeMs / v.decodedFrames : 0.0;
	const double gpu = v.gpuTimedFrames ? v.totalGpuDecodeTimeMs / v.gpuTimedFrames : 0.0;
	const double reassembly = v.decodedFrames ? v.totalReassemblyTimeUs / 1000.0 / v.decodedFrames : 0.0;
	const double render = v.renderedFrames ? v.totalRenderTimeUs / 1000.0 / v.renderedFrames : 0.0;
	const double present = v.countPresentDisplayMs ? v.totalPresentDisplayMs / v.countPresentDisplayMs : 0.0;
	const double pacer = v.renderedFrames ? v.totalPacerTimeUs / 1000.0 / v.renderedFrames : 0.0;
	const double host = v.framesWithHostProcessingLatency ? v.totalHostProcessingLatency / 10.0 / v.framesWithHostProcessingLatency : 0.0;
	const double networkDrop = v.totalFrames ? 100.0 * v.networkDroppedFrames / v.totalFrames : 0.0;
	const double pacerDrop = v.totalFrames ? 100.0 * v.pacerDroppedFrames / v.totalFrames : 0.0;
	const double frameBudget = v.totalFps > 0.0 ? 1000.0 / v.totalFps : 16.67;

	snprintf(m_videoMode, sizeof(m_videoMode), "%dx%d  /  %.1f FPS  /  %s", s.width, s.height, v.totalFps, s.codec);
	for (auto &tile : m_tiles) tile.color = White;
	auto &fps = m_tiles[0];
	snprintf(fps.value, sizeof(fps.value), "%.1f", v.renderedFps);
	snprintf(fps.unit, sizeof(fps.unit), "fps");
	snprintf(fps.detail, sizeof(fps.detail), "Received %.1f  /  decoded %.1f", v.receivedFps, v.decodedFps);
	fps.color = v.renderedFps < v.totalFps * 0.90 ? Red : v.renderedFps < v.totalFps * 0.98 ? Amber : Green;

	auto &dec = m_tiles[1];
	snprintf(dec.value, sizeof(dec.value), "%.2f", v.gpuTimedFrames ? gpu : decode);
	snprintf(dec.unit, sizeof(dec.unit), "ms");
	if (v.gpuTimedFrames) {
		snprintf(dec.detail, sizeof(dec.detail), "GPU  /  CPU submit %.2f ms", decode);
	} else {
		snprintf(dec.detail, sizeof(dec.detail), "%s", s.isPyroWave ? "CPU submit / GPU unavailable" : "Average per frame");
	}
	dec.color = v.decodedFrames ? latencyColor(v.gpuTimedFrames ? gpu : decode, frameBudget * 0.5, frameBudget) : Muted;
	if (!v.decodedFrames && !v.gpuTimedFrames) snprintf(dec.value, sizeof(dec.value), "--");

	auto &rtt = m_tiles[2];
	if (v.lastRtt) snprintf(rtt.value, sizeof(rtt.value), "%u", v.lastRtt);
	else snprintf(rtt.value, sizeof(rtt.value), "--");
	snprintf(rtt.unit, sizeof(rtt.unit), "ms");
	snprintf(rtt.detail, sizeof(rtt.detail), "RTT  \xC2\xB1 %u ms variance", v.lastRttVariance);
	rtt.color = v.lastRtt ? latencyColor(v.lastRtt + v.lastRttVariance, 20, 50) : Muted;

	auto &h = m_tiles[3];
	if (v.framesWithHostProcessingLatency) {
		snprintf(h.value, sizeof(h.value), "%.1f", host);
		snprintf(h.detail, sizeof(h.detail), "Min %.1f  /  max %.1f ms", v.minHostProcessingLatency / 10.0, v.maxHostProcessingLatency / 10.0);
		h.color = latencyColor(host, frameBudget * 0.75, frameBudget);
	} else {
		snprintf(h.value, sizeof(h.value), "--");
		snprintf(h.detail, sizeof(h.detail), "Not reported by host");
		h.color = Muted;
	}
	snprintf(h.unit, sizeof(h.unit), "ms");

	auto &rp = m_tiles[4];
	snprintf(rp.value, sizeof(rp.value), "%.2f / %.2f", render, present);
	snprintf(rp.unit, sizeof(rp.unit), "ms");
	snprintf(rp.detail, sizeof(rp.detail), "Render / present delay");
	rp.color = latencyColor(render + present, frameBudget, frameBudget * 2);

	auto &queue = m_tiles[5];
	snprintf(queue.value, sizeof(queue.value), "%.1f", s.averageQueueSize);
	snprintf(queue.unit, sizeof(queue.unit), "frames");
	snprintf(queue.detail, sizeof(queue.detail), "Pacer %.2f ms / %s", pacer, s.immediatePacing ? "immediate" : "display-locked");
	queue.color = latencyColor(s.averageQueueSize, 2, 4);

	auto &bitrate = m_tiles[6];
	snprintf(bitrate.value, sizeof(bitrate.value), "%.1f", s.averageMbps);
	snprintf(bitrate.unit, sizeof(bitrate.unit), "Mbps");
	snprintf(bitrate.detail, sizeof(bitrate.detail), "Peak %.1f Mbps / %u s", s.peakMbps, s.bitrateWindowSeconds);

	auto &drops = m_tiles[7];
	snprintf(drops.value, sizeof(drops.value), "%.2f / %.2f", networkDrop, pacerDrop);
	snprintf(drops.unit, sizeof(drops.unit), "%%");
	snprintf(drops.detail, sizeof(drops.detail), "Network / pacer");
	drops.color = latencyColor(std::max(networkDrop, pacerDrop), 0.5, 2.0);

	snprintf(m_secondary[0], sizeof(m_secondary[0]), "Packets %.1fK/s   /   Lost %llu   /   FEC recovered %llu (60s)",
	         v.videoPps / 1000.0, static_cast<unsigned long long>(s.lostPackets60s), static_cast<unsigned long long>(s.recoveredPackets60s));
	snprintf(m_secondary[1], sizeof(m_secondary[1]), "Reassembly %.2f ms   /   Audio buffer %.2f ms", reassembly, s.audioBufferMs);
	if (ImGuiPlots::instance().isEnabled()) {
		m_history[0][m_historyHead] = static_cast<float>(v.renderedFps);
		m_history[1][m_historyHead] = static_cast<float>(v.gpuTimedFrames ? gpu : decode);
		m_historyHead = (m_historyHead + 1) % 120;
		m_historyCount = std::min(m_historyCount + 1, 120);
	} else {
		m_historyCount = m_historyHead = 0;
	}
}

void StatsRenderer::DrawSparkline(int history, ImDrawList *draw, ImVec2 pos, ImVec2 size) {
	if (m_historyCount < 2) return;
	ImVec2 points[120];
	const int start = (m_historyHead + 120 - m_historyCount) % 120;
	float low = FLT_MAX, high = -FLT_MAX;
	for (int i = 0; i < m_historyCount; ++i) {
		float value = m_history[history][(start + i) % 120];
		low = std::min(low, value);
		high = std::max(high, value);
	}
	const float padding = std::max((high - low) * 0.25f, history == 0 ? 1.0f : 0.1f);
	low -= padding;
	high += padding;
	for (int i = 0; i < m_historyCount; ++i) {
		float value = m_history[history][(start + i) % 120];
		points[i] = ImVec2(pos.x + size.x * i / (m_historyCount - 1), pos.y + size.y * (1.0f - (value - low) / (high - low)));
	}
	draw->AddPolyline(points, m_historyCount, Green, 0, 1.5f * m_displayHeight / 1080.0f);
}

void StatsRenderer::RenderCard(bool showGraphs) {
	const float scale = m_displayHeight / 1080.0f;
	const float width = 540.0f * scale;
	const float height = (m_hasSnapshot ? 546.0f : 116.0f) * scale;
	ImGui::SetNextWindowPos(ImVec2(m_displayWidth * 0.05f, m_displayHeight * 0.05f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * scale);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f * scale);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(28 / 255.0f, 28 / 255.0f, 30 / 255.0f, 0.85f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.08f));
	if (ImGui::Begin("##OrbitPerformance", nullptr, OverlayFlags)) {
		ImDrawList *draw = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetWindowPos();
		auto text = [&](float x, float y, float size, ImU32 color, const char *value) {
			draw->AddText(ImGui::GetFont(), size * scale, ImVec2(origin.x + x * scale, origin.y + y * scale), color, value);
		};
		text(20, 12, 24, White, "Performance");
		text(20, 44, 14, Muted, m_hasSnapshot ? m_videoMode : "Waiting for stream statistics...");
		if (m_hasSnapshot) {
			const char *labels[] = {"Frame rate", "Decode time", "Network latency", "Host processing", "Render / present", "Frame queue", "Bitrate", "Dropped frames"};
			for (int i = 0; i < 8; ++i) {
				const float x = 20.0f + (i % 2) * 255.0f;
				const int row = i / 2;
				const float y = 72.0f + (row ? 110.0f + (row - 1) * 88.0f : 0.0f);
				const float tileHeight = row ? 78.0f : 100.0f;
				draw->AddRectFilled(ImVec2(origin.x + x * scale, origin.y + y * scale),
				                    ImVec2(origin.x + (x + 245) * scale, origin.y + (y + tileHeight) * scale), IM_COL32(255, 255, 255, 13), 8.0f * scale);
				text(x + 12, y + 6, 15, Muted, labels[i]);
				const Tile &tile = m_tiles[i];
				// Fit longer paired values without shrinking other tiles.
				const float valueSize = i == 4 || i == 7 ? 26.0f : 30.0f;
				text(x + 12, y + 25, valueSize, tile.color, tile.value);
				float valueWidth = ImGui::GetFont()->CalcTextSizeA(valueSize * scale, FLT_MAX, 0, tile.value).x / scale;
				text(x + 16 + valueWidth, y + 38, 14, Muted, tile.unit);
				text(x + 12, y + 58, 14, Muted, tile.detail);
				if (showGraphs && row == 0) {
					DrawSparkline(i, draw, ImVec2(origin.x + (x + 12) * scale, origin.y + (y + 79) * scale), ImVec2(221 * scale, 13 * scale));
				}
			}
			draw->AddLine(ImVec2(origin.x + 20 * scale, origin.y + 454 * scale), ImVec2(origin.x + 520 * scale, origin.y + 454 * scale), IM_COL32(255, 255, 255, 20));
			text(20, 467, 14, Muted, m_secondary[0]);
			text(20, 491, 14, Muted, m_secondary[1]);
		}
	}
	ImGui::End();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(2);
}

void StatsRenderer::Render(bool showGraphs) {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_visible) return;
	ZoneScopedN("StatsRender");
	TracyD3D11Zone(g_tracyCtx11, "StatsRender");
	// Use the actual ImGui viewport, which is also the D3D render target size.
	m_displayWidth = static_cast<uint32_t>(ImGui::GetIO().DisplaySize.x);
	m_displayHeight = static_cast<uint32_t>(ImGui::GetIO().DisplaySize.y);
	if (!m_displayWidth || !m_displayHeight) return;
	RenderCard(showGraphs);
	if (showGraphs) RenderGraphs();
}
struct clampData {
	const float *values;
	float maxVal;
};

static inline float clampGetter(void *data, int idx) {
	const clampData *c = static_cast<const clampData *>(data);
	return fminf(c->values[idx], c->maxVal);
}

void StatsRenderer::RenderGraphs() {
	// Reuse fixed sample storage, including on the first visible frame.
	static_assert(PlotCount == 8, "Update graph buffers when adding plots");
	static float buffers[8][512] = {};
	const float scale = m_displayHeight / 1080.0f;
	const float gap = 12.0f * scale;
	const float panelWidth = m_displayWidth * 0.9f - 560.0f * scale;
	const float graphW = (panelWidth - 32.0f * scale - 2 * gap) / 3;
	const float graphH = 110.0f * scale;
	const float itemSpacingX = gap;
	if (graphW <= 0.0f) return;

	// Share the card's TV-safe inset and leave a clear gutter between panels.
	ImGui::SetNextWindowPos(ImVec2(m_displayWidth * 0.05f + 560.0f * scale, m_displayHeight * 0.05f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(panelWidth, 310.0f * scale), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * scale);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * scale);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * scale, 16 * scale));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * scale, 6 * scale));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(28 / 255.0f, 28 / 255.0f, 30 / 255.0f, 0.85f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.08f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.89f, 1));
	ImGui::Begin("##OrbitGraphs", nullptr, OverlayFlags);
	ImGui::SetWindowFontScale(16.0f / 18.0f);
	ImGui::TextUnformatted("Performance history / min / max / average");

	auto draw_plot = [&](int i, float width, float height) {
		Plot &plot = ImGuiPlots::instance().get(i);

		float minY = 0.0f;
		float maxY = 0.0f;
		std::size_t countF = plot.buffer.copyInto(buffers[i], 512, minY, maxY);
		float avgF = plot.buffer.average();
		if (!countF) {
			ImGui::Dummy(ImVec2(width, height));
			return;
		}

		char label[64];
		switch (plot.desc.labelType) {
		case PLOT_LABEL_MIN_MAX_AVG:
			snprintf(label, sizeof(label), "%s  %.1f / %.1f / %.1f %s", plot.desc.title, minY, maxY, avgF, plot.desc.unit);
			break;
		case PLOT_LABEL_MIN_MAX_AVG_INT:
			snprintf(label, sizeof(label), "%s  %d / %d / %.1f %s", plot.desc.title, (int)minY, (int)maxY, avgF, plot.desc.unit);
			break;
		case PLOT_LABEL_TOTAL_INT:
			snprintf(label, sizeof(label), "%s  %d %s", plot.desc.title, (int)plot.buffer.sum(), plot.desc.unit);
			break;
		}
		float scaleMin = FLT_MAX;
		float scaleMax = FLT_MAX;
		if (plot.desc.scaleTarget != NULL) {
			// optionally center the graph on a target such as the ideal frametime
			float ideal = (float)plot.desc.scaleTarget;
			scaleMin = ideal - (2 * ideal);
			scaleMax = ideal + (2 * ideal);
		}
		if (plot.desc.scaleMin != NULL)
			scaleMin = plot.desc.scaleMin;
		if (plot.desc.scaleMax != NULL)
			scaleMax = plot.desc.scaleMax;

		if (i == PLOT_BANDWIDTH) {
			scaleMin = -0.1f;
			//scaleMax = ((float)m_BitrateKbps * 1.2) / 1024.0f;
			scaleMax = PyroWaveDecoder::instance().IsActive() ? 800.0f : 300.0f;
		}

		ImGui::PushID(i);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 0.05f));
		ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(61 / 255.0f, 220 / 255.0f, 132 / 255.0f, 1));
		if (plot.desc.clampMax != NULL) {
			clampData ctx{buffers[i], plot.desc.clampMax};
			ImGui::PlotLines("##xx", clampGetter, &ctx, countF, 0, (countF > 0 ? label : "no data"), scaleMin, scaleMax, ImVec2(width, height));
		} else {
			ImGui::PlotLines("##xx", buffers[i], countF, 0, (countF > 0 ? label : "no data"), scaleMin, scaleMax, ImVec2(width, height));
		}
		ImGui::PopStyleColor(2);
		ImGui::PopID();
	};

	const int row1[3] = {PLOT_FRAMETIME, PLOT_DROPPED_NETWORK, PLOT_QUEUED_FRAMES};
	for (int c = 0; c < 3; ++c) {
		if (c > 0) ImGui::SameLine(0.0f, itemSpacingX);
		draw_plot(row1[c], graphW, graphH);
	}

	ImGui::Dummy(ImVec2(1.0f, 0.0f));
	const int row2[3] = {PLOT_HOST_FRAMETIME, PLOT_DROPPED_PACER, PLOT_BANDWIDTH};
	for (int c = 0; c < 3; ++c) {
		if (c > 0) ImGui::SameLine(0.0f, itemSpacingX);
		draw_plot(row2[c], graphW, graphH);
	}

	// 3rd row for quickly graphing something if needed
	// ImGui::Dummy(ImVec2(1.0f, 0.0f));
	// draw_plot(PLOT_AUDIO_BUFFER_MS, graphW, graphH);

	ImGui::End();
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(5);
}

void StatsRenderer::CreateDeviceDependentResources() {
	CreateWindowSizeDependentResources();
}

void StatsRenderer::CreateWindowSizeDependentResources() {
	m_deviceResources->GetUWPPixelDimensions(&m_displayWidth, &m_displayHeight);
}

void StatsRenderer::ReleaseDeviceDependentResources() {
	// ImGui's atlas and DX11 resources are owned by DeviceResources.
}

bool StatsRenderer::GetVisible() {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_visible;
}

void StatsRenderer::SetVisible(bool visible) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_visible = visible;
}

void StatsRenderer::ToggleVisible() {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_visible = !m_visible;
}

#include <thread>
#include <cmath>
#include "../memory/memory.hpp"
#include "../classes/globals.hpp"
#include "../classes/render.hpp"
#include "../classes/config.hpp"

namespace hack {
	std::shared_ptr<pProcess> process;
	ProcessModule base_client;
	ProcessModule base_engine;

	void loop() {
		const uintptr_t localPlayer = process->read<uintptr_t>(base_client.base + updater::offsets::dwLocalPlayer);
		if (!localPlayer)
			return;

		const int localTeam = process->read<int>(localPlayer + updater::offsets::m_iTeamNum);
		const view_matrix_t view_matrix = process->read<view_matrix_t>(base_client.base + updater::offsets::dwViewMatrix);
		const uintptr_t entity_list = process->read<uintptr_t>(base_client.base + updater::offsets::dwEntityList);

		const std::uint32_t localPlayerPawn = process->read<std::uint32_t>(localPlayer + updater::offsets::dwPlayerPawn);
		if (!localPlayerPawn)
			return;
		const uintptr_t localList_entry2 = process->read<uintptr_t>(entity_list + 0x8 * ((localPlayerPawn & 0x7FFF) >> 9) + 16);
		const uintptr_t localpCSPlayerPawn = process->read<uintptr_t>(localList_entry2 + 120 * (localPlayerPawn & 0x1FF));
		if (!localpCSPlayerPawn)
			return;

		const Vector3 localOrigin = process->read<Vector3>(localpCSPlayerPawn + updater::offsets::m_vecOrigin);

		int playerIndex = 0;
		uintptr_t list_entry;

		/**
		* Loop through all the players in the entity list
		*
		* (This could have been done by getting a entity list count from the engine, but I'm too lazy to do that)
		**/
		while (true) {
			bool c4IsPlanted = process->read<bool>(base_client.base + updater::offsets::dwPlantedC4 - 0x8);
			if (c4IsPlanted)
			{
				const uintptr_t planted_c4 = process->read<uintptr_t>(process->read<uintptr_t>(base_client.base + updater::offsets::dwPlantedC4));

				static auto plantTime = std::chrono::high_resolution_clock::now();
				auto currentTime = std::chrono::high_resolution_clock::now();
				auto elapsedTime = std::chrono::duration<float>(currentTime - plantTime).count();

				const float c4Timer = process->read<float>(planted_c4 + updater::offsets::m_flTimerLength);
				const float c4Beep = process->read<float>(planted_c4 + updater::offsets::m_flNextBeep);
				const float c4Blow = process->read<float>(planted_c4 + updater::offsets::m_flC4Blow);
				float remainingTime = (c4Blow - c4Beep) / c4Timer; // Constrained to 0.0-1.0 (0.0 being explosion imminent)
				remainingTime = remainingTime * 100; // Multiply by 100 to constrain to 0.0-100.00

				const uintptr_t c4Node = process->read<uintptr_t>(planted_c4 + updater::offsets::m_pGameSceneNode);

				const Vector3 c4Origin = process->read<Vector3>(c4Node + updater::offsets::m_vecAbsOrigin);

				const Vector3 c4ScreenPos = c4Origin.world_to_screen(view_matrix);

				float distance = localOrigin.calculate_distance(c4Origin);
				float roundedDistance = std::round(distance / 500.f);

				float height = 10 - roundedDistance;
				float width = height * 1.2f;

				render::DrawBorderBox(
					g::hdcBuffer,
					c4ScreenPos.x - (width / 2),
					c4ScreenPos.y - (height / 2),
					width,
					height,
					RGB((255 - remainingTime), (55 + remainingTime * 2), 75)
				);

				render::RenderText(
					g::hdcBuffer,
					c4ScreenPos.x + (width / 2 + 5),
					c4ScreenPos.y,
					("C4 " + std::to_string(remainingTime)).c_str(),
					config::esp_name_color,
					10
				);
			}
			
			playerIndex++;
			list_entry = process->read<uintptr_t>(entity_list + (8 * (playerIndex & 0x7FFF) >> 9) + 16);
			if (!list_entry)
				break;

			const uintptr_t player = process->read<uintptr_t>(list_entry + 120 * (playerIndex & 0x1FF));
			if (!player)
				continue;

			/**
			* Skip rendering your own character and teammates
			*
			* If you really want you can exclude your own character from the check but
			* since you are in the same team as yourself it will be excluded anyway
			**/
			const int playerTeam = process->read<int>(player + updater::offsets::m_iTeamNum);
			if (config::team_esp && (playerTeam == localTeam))
				continue;


			const std::uint32_t playerPawn = process->read<std::uint32_t>(player + updater::offsets::dwPlayerPawn);

			const uintptr_t list_entry2 = process->read<uintptr_t>(entity_list + 0x8 * ((playerPawn & 0x7FFF) >> 9) + 16);
			if (!list_entry2)
				continue;

			const uintptr_t pCSPlayerPawn = process->read<uintptr_t>(list_entry2 + 120 * (playerPawn & 0x1FF));
			if (!pCSPlayerPawn)
				continue;

			const int playerHealth = process->read<int>(pCSPlayerPawn + updater::offsets::m_iHealth);
			const int playerArmor = process->read<int>(pCSPlayerPawn + updater::offsets::m_ArmorValue);
			if (playerHealth <= 0 || playerHealth > 100)
				continue;

			if (config::team_esp && (pCSPlayerPawn == localPlayer))
				continue;

			std::string playerName = "Invalid Name";
			const DWORD64 playerNameAddress = process->read<DWORD64>(player + updater::offsets::dwSanitizedName);

			if (playerNameAddress) {
				char buf[256];
				process->read_raw(playerNameAddress, buf, sizeof(buf));
				playerName = std::string(buf);
			}

			const auto clippingWeapon = process->read<std::uint64_t>(pCSPlayerPawn + updater::offsets::m_pClippingWeapon);
			const auto weaponData = process->read<std::uint64_t>(clippingWeapon + 0x360);
			const auto weaponNameAddress = process->read<std::uint64_t>(weaponData + updater::offsets::m_szName);
			std::string weaponName = "Invalid Weapon Name";

			if (!weaponNameAddress) {
				weaponName = "Invalid Weapon Name";
			}
			else {
				char buf[32];
				process->read_raw(weaponNameAddress, buf, sizeof(buf));
				weaponName = std::string(buf);
			}

			const Vector3 origin = process->read<Vector3>(pCSPlayerPawn + updater::offsets::m_vecOrigin);
			const Vector3 head = { origin.x, origin.y, origin.z + 75.f };
			const Vector3 head2 = { origin.x + 10, origin.y + 10, origin.z + 75.f };

			if (config::render_distance != -1 && (localOrigin - origin).length2d() > config::render_distance)
				continue;

			if ((localOrigin - origin).length2d() < 10)
				continue;

			const Vector3 screenPos = origin.world_to_screen(view_matrix);
			const Vector3 screenHead = head.world_to_screen(view_matrix);

			const float height = screenPos.y - screenHead.y;
			const float width = height / 2.4f;

			const float head_height = (screenPos.y - screenHead.y) / 8;
			const float head_width = (height / 2.4f) / 4;

			if (screenPos.z >= 0.01f) {
				float distance = localOrigin.calculate_distance(origin);
				int roundedDistance = std::round(distance / 10.f);

				render::DrawBorderBox(
					g::hdcBuffer,
					screenHead.x - width / 2,
					screenHead.y,
					width,
					height,
					(localTeam == playerTeam ? config::esp_box_color_team : config::esp_box_color_enemy)
				);

				render::DrawBorderBox(
					g::hdcBuffer,
					screenHead.x - (width / 2 + 10),
					screenHead.y + (height * (100 - playerArmor) / 100),
					2,
					height - (height * (100 - playerArmor) / 100),
					RGB(0, 185, 255)
				);

				render::DrawBorderBox(
					g::hdcBuffer,
					screenHead.x - (width / 2 + 5),
					screenHead.y + (height * (100 - playerHealth) / 100),
					2,
					height - (height * (100 - playerHealth) / 100),
					RGB(
						(255 - playerHealth),
						(55 + playerHealth * 2),
						75
					)
				);

				render::RenderText(
					g::hdcBuffer,
					screenHead.x + (width / 2 + 5),
					screenHead.y,
					playerName.c_str(),
					config::esp_name_color,
					10
				);

				/**
				* I know is not the best way but a simple way to not saturate the screen with a ton of information
				*/
				if (roundedDistance > config::flag_render_distance)
					continue;

				render::RenderText(
					g::hdcBuffer,
					screenHead.x + (width / 2 + 5),
					screenHead.y + 10,
					(std::to_string(playerHealth) + "hp").c_str(),
					RGB(
						(255 - playerHealth),
						(55 + playerHealth * 2),
						75
					),
					10
				);

				render::RenderText(
					g::hdcBuffer,
					screenHead.x + (width / 2 + 5),
					screenHead.y + 20,
					(std::to_string(playerArmor) + "armor").c_str(),
					RGB(
						(255 - playerArmor),
						(55 + playerArmor * 2),
						75
					),
					10
				);

				if (config::show_extra_flags)
				{
					const bool isDefusing = process->read<bool>(pCSPlayerPawn + updater::offsets::m_bIsDefusing);

					render::RenderText(
						g::hdcBuffer,
						screenHead.x + (width / 2 + 5),
						screenHead.y + 30,
						weaponName.c_str(),
						config::esp_distance_color,
						10
					);

					render::RenderText(
						g::hdcBuffer,
						screenHead.x + (width / 2 + 5),
						screenHead.y + 40,
						(std::to_string(roundedDistance) + "m away").c_str(),
						config::esp_distance_color,
						10
					);

					std::string defuText = "Player is defusing";

					if (isDefusing)
					{
						render::RenderText(
							g::hdcBuffer,
							screenHead.x + (width / 2 + 5),
							screenHead.y + 50,
							defuText.c_str(),
							config::esp_distance_color,
							10
						);
					}
				}

				/**
				* Great idea by @ifBars that will be implemented later on
				*/
				/*
					if (config::head_tracker)
					{
						if (roundedDistance > 35)
						{
							render::DrawFilledBox(
								g::hdcBuffer,
								screenHead.x - (head_width / 2),
								(screenHead.y + 10 - (roundedDistance / 80)) - (head_height / 2),
								head_width,
								head_height,
								(localTeam == playerTeam ? RGB(75, 175, 75) : RGB(175, 75, 75))
							);
						}
						else if (roundedDistance > 5)
						{
							render::DrawFilledBox(
								g::hdcBuffer,
								screenHead.x - (head_width / 2),
								screenHead.y + 25 - (head_height / 2),
								head_width,
								head_height,
								(localTeam == playerTeam ? RGB(75, 175, 75) : RGB(175, 75, 75))
							);
						}
					}
				*/
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

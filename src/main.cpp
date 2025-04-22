//================================================================================================
/// @file main.cpp
///
/// @brief Defines `main` for the seeder example
/// @details This example is meant to use all the major protocols in a more "complete" application.
/// @author Adrian Del Grosso
///
/// @copyright 2023 The Open-Agriculture Developers
//================================================================================================
#include "isobus/hardware_integration/can_hardware_interface.hpp"
#include "isobus/hardware_integration/twai_plugin.hpp"
#include "isobus/isobus/can_general_parameter_group_numbers.hpp"
#include "isobus/isobus/can_network_manager.hpp"
#include "isobus/isobus/can_partnered_control_function.hpp"
#include "isobus/isobus/can_stack_logger.hpp"
#include "isobus/isobus/isobus_diagnostic_protocol.hpp"
#include "isobus/isobus/isobus_standard_data_description_indices.hpp"
#include "isobus/isobus/isobus_task_controller_client.hpp"
#include "isobus/isobus/isobus_virtual_terminal_client.hpp"
#include "isobus/isobus/isobus_virtual_terminal_client_update_helper.hpp"
#include "isobus/utility/iop_file_interface.hpp"
#include "esp_log.h"
#include "isobus/isobus/isobus_diagnostic_protocol.hpp"
#include "vt_application.hpp"

#include "console_logger.cpp"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "object_Pool.hpp"
// led
/*
#include "Freenove WS2812 Lib for ESP32/src/Freenove_WS2812_Lib_for_ESP32.h"
#define LEDS_COUNT  3
#define LEDS_PIN	2
#define CHANNEL		0
Freenove_ESP32_WS2812 strip = Freenove_ESP32_WS2812(LEDS_COUNT, LEDS_PIN, CHANNEL, TYPE_GRB);
*/
//led
#include <functional>
#include <iostream>
#include <memory>
#include "esp_mac.h"


std::atomic_bool running = { true };


//static std::unique_ptr<SeederVtApplication> VTApplication = nullptr;
static std::unique_ptr<isobus::DiagnosticProtocol> diagnosticProtocol = nullptr;
static std::unique_ptr<SeederVtApplication> VTApplication = nullptr;

void signal_handler(int)
{
	running = false;
}
 
extern "C" const uint8_t object_pool_start[] asm("_binary_object_pool_iop_start");
extern "C" const uint8_t object_pool_end[] asm("_binary_object_pool_iop_end");

extern "C" void app_main()
{
	
	// Automatically load the desired CAN driver based on the available drivers
	twai_general_config_t twaiConfig = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_17, GPIO_NUM_16, TWAI_MODE_NORMAL);
	twai_timing_config_t twaiTiming = TWAI_TIMING_CONFIG_250KBITS();
	twai_filter_config_t twaiFilter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
	std::shared_ptr<isobus::CANHardwarePlugin> canDriver = std::make_shared<isobus::TWAIPlugin>(&twaiConfig, &twaiTiming, &twaiFilter);

	isobus::CANStackLogger::set_can_stack_logger_sink(&logger);
	isobus::CANStackLogger::set_log_level(isobus::CANStackLogger::LoggingLevel::Debug); // Change this to Debug to see more information
	isobus::CANHardwareInterface::set_number_of_can_channels(1);
	isobus::CANHardwareInterface::assign_can_channel_frame_handler(0, canDriver);
	// isobus::CANHardwareInterface::set_periodic_update_interval(10); // 10ms update period matches the default FreeRTOS tick rate of 100Hz

	if (!isobus::CANHardwareInterface::start() || !canDriver->get_is_valid())
	{
		ESP_LOGE("AgIsoStack", "Failed to start hardware interface, the CAN driver might be invalid");
		if (nullptr != VTApplication)
		{
			VTApplication->VTClientInterface->terminate();
			VTApplication->TCClientInterface.terminate();
		}
		if (nullptr != diagnosticProtocol)
		{
			diagnosticProtocol->terminate();
		}
		isobus::CANHardwareInterface::stop();
	}

	isobus::NAME TestDeviceNAME(0);

	//! This is an example device that is using a manufacturer code that is currently unused at time of writing
	TestDeviceNAME.set_arbitrary_address_capable(true);
	TestDeviceNAME.set_industry_group(2);
	TestDeviceNAME.set_device_class(4);
	TestDeviceNAME.set_function_code(static_cast<std::uint8_t>(isobus::NAME::Function::RateControl));
	TestDeviceNAME.set_identity_number(2);
	TestDeviceNAME.set_ecu_instance(0);
	TestDeviceNAME.set_function_instance(0);
	TestDeviceNAME.set_device_class_instance(0);
	TestDeviceNAME.set_manufacturer_code(1407);

	const isobus::NAMEFilter filterVirtualTerminal(isobus::NAME::NAMEParameters::FunctionCode, static_cast<std::uint8_t>(isobus::NAME::Function::VirtualTerminal));
	const isobus::NAMEFilter filterTaskController(isobus::NAME::NAMEParameters::FunctionCode, static_cast<std::uint8_t>(isobus::NAME::Function::TaskController));
	const isobus::NAMEFilter filterTaskControllerInstance(isobus::NAME::NAMEParameters::FunctionInstance, 0);
	const isobus::NAMEFilter filterTaskControllerIndustryGroup(isobus::NAME::NAMEParameters::IndustryGroup, static_cast<std::uint8_t>(isobus::NAME::IndustryGroup::AgriculturalAndForestryEquipment));
	const isobus::NAMEFilter filterTaskControllerDeviceClass(isobus::NAME::NAMEParameters::DeviceClass, static_cast<std::uint8_t>(isobus::NAME::DeviceClass::NonSpecific));
	const std::vector<isobus::NAMEFilter> tcNameFilters = { filterTaskController,
		                                                      filterTaskControllerInstance,
		                                                      filterTaskControllerIndustryGroup,
		                                                      filterTaskControllerDeviceClass };
	const std::vector<isobus::NAMEFilter> vtNameFilters = { filterVirtualTerminal };
	auto InternalECU = isobus::CANNetworkManager::CANNetwork.create_internal_control_function(TestDeviceNAME, 0);
	auto PartnerVT = isobus::CANNetworkManager::CANNetwork.create_partnered_control_function(0, vtNameFilters);
	auto PartnerTC = isobus::CANNetworkManager::CANNetwork.create_partnered_control_function(0, tcNameFilters);

	diagnosticProtocol = std::make_unique<isobus::DiagnosticProtocol>(InternalECU);
	diagnosticProtocol->initialize();

	diagnosticProtocol->set_product_identification_code("1234567890ABC");
	diagnosticProtocol->set_product_identification_brand("AgIsoStack++");
	diagnosticProtocol->set_product_identification_model("AgIsoStack++ Seeder Example");
	diagnosticProtocol->set_software_id_field(0, "Example 1.0.0");
	diagnosticProtocol->set_ecu_id_field(isobus::DiagnosticProtocol::ECUIdentificationFields::HardwareID, "1234");
	diagnosticProtocol->set_ecu_id_field(isobus::DiagnosticProtocol::ECUIdentificationFields::Location, "N/A");
	diagnosticProtocol->set_ecu_id_field(isobus::DiagnosticProtocol::ECUIdentificationFields::ManufacturerName, "Open-Agriculture");
	diagnosticProtocol->set_ecu_id_field(isobus::DiagnosticProtocol::ECUIdentificationFields::PartNumber, "1234");
	diagnosticProtocol->set_ecu_id_field(isobus::DiagnosticProtocol::ECUIdentificationFields::SerialNumber, "2");
	diagnosticProtocol->ControlFunctionFunctionalitiesMessageInterface.set_task_controller_geo_client_option(255);
	diagnosticProtocol->ControlFunctionFunctionalitiesMessageInterface.set_task_controller_section_control_client_option_state(1, 255);
	diagnosticProtocol->ControlFunctionFunctionalitiesMessageInterface.set_functionality_is_supported(isobus::ControlFunctionFunctionalities::Functionalities::MinimumControlFunction, 1, true);
	diagnosticProtocol->ControlFunctionFunctionalitiesMessageInterface.set_functionality_is_supported(isobus::ControlFunctionFunctionalities::Functionalities::UniversalTerminalWorkingSet, 1, true);
	diagnosticProtocol->ControlFunctionFunctionalitiesMessageInterface.set_functionality_is_supported(isobus::ControlFunctionFunctionalities::Functionalities::TaskControllerBasicClient, 1, true);
	diagnosticProtocol->ControlFunctionFunctionalitiesMessageInterface.set_functionality_is_supported(isobus::ControlFunctionFunctionalities::Functionalities::TaskControllerGeoClient, 1, true);
	diagnosticProtocol->ControlFunctionFunctionalitiesMessageInterface.set_functionality_is_supported(isobus::ControlFunctionFunctionalities::Functionalities::TaskControllerSectionControlClient, 1, true);

	VTApplication = std::make_unique<SeederVtApplication>(PartnerVT, PartnerTC, InternalECU);
	VTApplication->initialize();

		while (running)
		if (nullptr != VTApplication)
		{
			
			VTApplication->update();
			diagnosticProtocol->update();
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		
}

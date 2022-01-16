/*
 * Copyright (C) 2017 Opendigitalradio (http://www.opendigitalradio.org/)
 * Copyright (C) 2017 Felix Morgner <felix.morgner@hsr.ch>
 * Copyright (C) 2017 Tobias Stauber <tobias.stauber@hsr.ch>
 * Copyright (C) 2021 - 2022 Bastiaan Teeuwen <bastiaan@mkcl.nl>
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <dab/packet/packet_parser.h>
#include <dab/msc_data_group/msc_data_group_parser.h>
#include <dab/ensemble/ensemble.h>
#include <dab/ensemble/service.h>
#include <dab/ensemble/service_component.h>
#include <dab/demodulator.h>
#include <dab/device/device.h>
#include <dab/device/rtl_device.h>
#include <dab/types/common_types.h>
#include <dab/types/gain.h>

#include <asio/io_service.hpp>
#include <asio/signal_set.hpp>

#include <cstdint>
#include <cstring>
#include <future>
#include <iostream>
#include <fstream>

#include <unistd.h>

constexpr char progname[] = "dab-datarecv";

// Make _kHz and co usable
using namespace dab::literals;

//ensemblename tied to the frequency
const std::map<std::string,dab::frequency> channels = {
	{"5A", 174928_kHz},{"5B", 176640_kHz},{"5C", 178352_kHz},{"5D", 180064_kHz},
	{"6A", 181936_kHz},{"6B", 183648_kHz},{"6C", 185360_kHz},{"6D", 187072_kHz},
	{"7A", 188928_kHz},{"7B", 190640_kHz},{"7C", 192352_kHz},{"7D", 194064_kHz},
	{"8A", 195936_kHz},{"8B", 197648_kHz},{"8C", 199360_kHz},{"8D", 201072_kHz},
	{"9A", 202928_kHz},{"9B", 204640_kHz},{"9C", 206352_kHz},{"9D", 208064_kHz},
	{"10A",209936_kHz},{"10B", 211648_kHz},{"10C", 213360_kHz},{"10D", 215072_kHz},
	{"11A", 216928_kHz},{"11B", 218640_kHz},{"11C", 220352_kHz},{"11D", 222064_kHz},
	{"12A", 223936_kHz},{"12B", 225648_kHz},{"12C", 227360_kHz},{"12D", 229072_kHz},
	{"13A", 230784_kHz},{"13B", 232496_kHz},{"13C", 234208_kHz},{"13D", 235776_kHz},
	{"13E", 237488_kHz},{"13F", 239200_kHz}
};

void usage(int retval)
{
	std::cout <<
		"usage: " << progname << " frequency:service_label:packet_address\n" <<
		"\n" <<
		"" << retval << std::endl;
}

#if 0
int main(int argc, char **argv)
{
	if (argc < 2) {
		return usage(1);
	}

	/* parse the frequency, service label and packet address */
	char *s;
	unsigned long freq;

	if (!(s = std::strtok(argv[1], ":")))
		return usage(1);
	try {
		freq = std::stoul(s, nullptr, 10);
	} catch (std::invalid_argument e) {
		std::cout << progname << ": frequency must be in kHz" << std::endl;
		return 1;
	}

	if (!(s = std::strtok(nullptr, ":")))
		return usage(1);
	std::string label(s);

	if (!(s = std::strtok(nullptr, ":")))
		return usage(2);
	std::string addr{s};

	std::cout <<freq<<label<<addr<<std::endl;

	while ((auto opt == getopt(argc, argv, "a")) != -1) {
		switch (opt) {
		case "a":
			std::cout << "!";
			break;
		}
	}

	return 0;
}
#else
static int dabmsg_id = 0;

bool save_dab_message(std::vector<uint8_t> &data)
{
	std::ofstream dabmsg("dabmsgs/" + std::to_string(dabmsg_id) + ".txt");
	dabmsg_id++;

	if (!dabmsg.is_open())
		return false;

	/* Write the message to a file for use with cfns-half-duplex */
	dabmsg << dabmsg_id << std::endl;	/* ID: increments with every received message */
	dabmsg << 4 << std::endl;		/* TYPE: always 4 for some reason */
	dabmsg << "other" << std::endl;		/* CATEGORY: to be determined later */
	for (auto &byte : data)
		dabmsg << byte;			/* DATA */

	dabmsg.close();

	return true;
}

int main(int argc, char **argv)
{
	// Very crude argument handling. DON'T USE THIS IN PRODUCTION!
	if(argc != 2) {
		std::cerr << "usage: receiver <packet_address>\n";
		return 1;
	}

	// Prepare are data queues for acquisition and demodulation
	dab::sample_queue_t samples{};
	dab::symbol_queue_t symbols{};

	// Prepare the input device
	dab::rtl_device device{samples};
	device.enable(dab::device::option::automatic_gain_control);
	device.tune(channels.find("8B")->second);

	// Start sample acquisition
	auto deviceRunner = std::async(std::launch::async, [&]{ device.run(); });

	// Initialize the demodulator
	dab::demodulator demod{samples, symbols, dab::kTransmissionMode1};
	auto demodRunner = std::async(std::launch::async, [&]{ demod.run(); });

	std::clog << "Connecting...";

	// Initialize the decoder
	dab::ensemble ensemble(symbols, dab::kTransmissionMode1);

	// Prepare our packet parser
	auto packetParser = dab::packet_parser{std::uint16_t(std::stoi(argv[1]))};

	// Get the ensemble ready
	while (!ensemble && ensemble.update());

	// Check if we were able to succcessfully prepare the ensemble
	if (!ensemble) {
		std::clog << " FAIL\n";
		return 1;
	}

	std::clog << " OK\n";
	std::clog << "Name: " << ensemble.label() << std::endl;
	std::clog << "Waiting for messages..." << std::endl;

	for (auto const & service : ensemble.services()) {
		std::clog << "service \"" << service.second->label() << "\" t:" << (std::uint8_t) service.second->type() << "\n";

		/* Check for a data service with a valid primary service component */
		if (service.second->type() != dab::service_type::data || !service.second->primary())
			continue;

		/* Check if the primary service component claims to carry IPDT */
		if (service.second->primary()->type() != 59)
			continue;

		std::clog << "Registering service \"" << service.second->label() << "\"\n";

		/* Register our "data received" callback with the service */
		ensemble.activate(service.second, [&] (std::vector<std::uint8_t> data)
		{
			/* Parse the received data */
			auto packet = packetParser.parse(data);

			/* Not all data has been received yet */
			if (packet.first == dab::parse_status::incomplete)
				return;

			/* Return if this is not the right address (likely noise) */
			if (packet.first == dab::parse_status::invalid_address)
				return;

			if (packet.first != dab::parse_status::ok) {
				std::cerr << "packetError: " << std::uint32_t(packet.first) << std::endl;
				return;
			}

			/* Parse the received data back into an MSC data group */
			auto datagroupParser = dab::msc_data_group_parser{};
			auto datagroup = datagroupParser.parse(packet.second);

			if (datagroup.first != dab::parse_status::ok) {
				std::cerr << "datagroupError: " << std::uint32_t(datagroup.first) << std::endl;
				return;
			}

			/* FIXME HACKZZZ: fix the parser instead of this workaround
			 *                is this padding?
			 * FIXME there's nicer ways to do this of course
			 */
			int bytecnt = 0, skip = 0;
			std::vector<uint8_t> dabmsg;
			for (unsigned char c : std::move(datagroup.second)) {
				bytecnt++;

				if (skip > 0) {
					skip--;
					continue;
				}

				if (bytecnt == 1024)
					skip = 6;

				dabmsg.push_back(c);
			}

			if (!save_dab_message(dabmsg))
				std::cerr << "failed to write dabmsg" << std::endl;
		});
	}

	while (ensemble.update());

	/* properly shutdown demodulator, device, ensemble on Ctrl+C */

	return 0;
}

#endif

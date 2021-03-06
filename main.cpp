/*
 * Copyright (c) 2014, 2015 Ahmed Samy  <f.fallen45@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <ctorrent/torrent.h>
#include <net/connection.h>
#include <util/auxiliar.h>

#include <thread>
#include <functional>
#include <chrono>
#include <iostream>
#include <fstream>
#include <boost/program_options.hpp>

#ifndef _WIN32
#include <ncurses.h>
#endif

/* Externed  */
std::ofstream logfile;

#ifdef _WIN32
enum {
	COL_BLACK = 0,
	COL_GREEN = 10,
	COL_YELLOW = 14,
};

static void set_color(int col)
{
	CONSOLE_SCREEN_BUFFER_INFO i;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &i);

	int back = (i.wAttributes / 16) % 16;
	if (col % 16 == back % 16)
		++col;
	col %= 16;
	back %= 16;

	uint16_t attrib = ((unsigned)back << 4) | (unsigned)col;
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), attrib);
}

#define printc(c, fmt, args...) do {	\
	set_color((c));	\
	printf(fmt, ##args);	\
	set_color(COL_BLACK);	\
} while (0)
#else
enum {
	COL_GREEN = 1,
	COL_YELLOW = 2,
};

#define printc(c, fmt, args...)	do {	\
	attron(COLOR_PAIR((c))); 	\
	printw(fmt, ##args); 		\
} while (0)
#endif

static void print_stats(Torrent *t)
{
	TorrentMeta *meta = t->meta();
	TorrentFileManager *fm = t->fileManager();

	printc(COL_GREEN, "\r%s: ", meta->name().c_str());
	printc(COL_YELLOW, "%.2f Mbps (%zd/%zd MB) [ %zd uploaded - %zd hash miss - %zd wasted - %.2f seconds left ] ",
				t->downloadSpeed(), t->computeDownloaded() / 1024 / 1024, meta->totalSize() / 1024 / 1024,
				t->uploadedBytes(), t->hashMisses(), t->wastedBytes(), t->eta());
	printc(COL_YELLOW, "[ %zd/%zd/%zd pieces %zd peers active ]\n",
				fm->completedPieces(), fm->pending(), fm->totalPieces(), t->activePeers());
}

static void print_all_stats(Torrent *torrents, size_t total)
{
#ifdef _WIN32
	COORD coord;
	CONSOLE_SCREEN_BUFFER_INFO info;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
	coord.X = info.dwCursorPosition.X;
	coord.Y = info.dwCursorPosition.Y;
#else
	move(0, 0);
#endif
	for (size_t i = 0; i < total; ++i)
		print_stats(&torrents[i]);
#ifdef _WIN32
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
#else
	printw("\n");
	refresh();
#endif
}

int main(int argc, char *argv[])
{
	bool noseed = true;
	bool nodownload = false;
	int startport = 6881;
	size_t max_peers = 30;
	std::string dldir = "Torrents";
	std::string lfname = "out.txt";
	std::vector<std::string> files;

	namespace po = boost::program_options;
	po::options_description opts;
	opts.add_options()
		("version,v", "print version string")
		("help,h", "print this help message")
		("port,p", po::value(&startport), "specify start port for seeder torrents")
		("peers,m", po::value(&max_peers), "maximum amount of peers to feel sufficient with, 0 implies as many as possible")
		("nodownload,n", po::bool_switch(&nodownload), "do not download anything, just print info about torrents")
		("piecesize,s", po::value(&maxRequestSize), "specify piece block size")
		("dldir,d", po::value(&dldir), "specify downloads directory")
		("noseed,e", po::bool_switch(&noseed), "do not seed after download has finished.")
		("log,l", po::value(&lfname), "specify log file name")
		("torrents,t", po::value<std::vector<std::string>>(&files)->required()->multitoken(), "specify torrent file(s)");

	if (argc == 1) {
		std::clog << opts << std::endl;
		std::clog << "Example: " << argv[0] << " --nodownload --torrents a.torrent b.torrent c.torrent" << std::endl;
		return 1;
	}

	po::variables_map vm;
	try {
		po::store(po::parse_command_line(argc, argv, opts), vm);
		po::notify(vm);
	} catch (const std::exception &e) {
		std::cerr << argv[0] << ": error parsing command line arguments: " << e.what() << std::endl;
		std::clog << opts << std::endl;
		std::clog << "Example: " << argv[0] << " --nodownload --torrents a.torrent b.torrent c.torrent" << std::endl;
		return 1;
	}

	if (vm.count("help")) {
		std::clog << opts << std::endl;
		std::clog << "Example: " << argv[0] << " --nodownload --torrents a.torrent b.torrent c.torrent" << std::endl;
		return 0;
	}

	if (vm.count("version")) {
		std::clog << "CTorrent version 1.0" << std::endl;
		return 0;
	}

	if (vm.count("piecesize"))
		maxRequestSize = 1 << (32 - __builtin_clz(maxRequestSize - 1));

	logfile.open(lfname, std::ios_base::trunc | std::ios_base::out);
	if (!logfile.is_open()) {
		std::cerr << "Cannot open log file " << lfname << ": " << errno << std::endl;
		return 1;
	}

	size_t total = files.size();
	size_t total_bits = 0;
	size_t completed = 0;
	size_t errors = 0;
	size_t eseed = 0;
	size_t started = 0;

	std::vector<Torrent> torrents(total);
	for (size_t i = 0; i < total; ++i) {
		std::string file = files[i];
		Torrent *t = &torrents[i];
		total_bits |= 1 << i;

		std::clog << "Scanning: " << file << "... ";
		if (!t->open(file, dldir)) {
			std::cerr << "corrupted torrent file" << std::endl;
			errors |= 1 << i;
			continue;
		}
		std::clog << "Done" << std::endl;

		const TorrentMeta *meta = t->meta();
		if (nodownload) {
			const TorrentFileManager *fm = t->fileManager();

			std::clog << meta->name() << ": Total size: " << bytesToHumanReadable(meta->totalSize(), true) << std::endl;
			std::clog << meta->name() << ": Completed pieces: " << fm->completedPieces() << "/" << fm->totalPieces() << std::endl;
			std::clog << meta->name() << ": Piece Length: " << meta->pieceLength() << std::endl;
			completed |= 1 << i;
			continue;
		}

		std::clog << "Preparing " << file << "... ";
		Torrent::DownloadState state = t->prepare(startport++, !noseed);
		switch (state) {
		case Torrent::DownloadState::None:
			++started;
			std::clog << "Done" << std::endl;
			break;
		case Torrent::DownloadState::Completed:
			completed |= 1 << i;
			std::clog << "Done (already downloaded)" << std::endl;
			break;
		default:
			errors |= 1 << i;
			std::cerr << "Failed: " << (int)state << std::endl;
			break;
		}
	}

#ifndef _WIN32
	usleep(10000);
	initscr();
	assume_default_colors(COLOR_WHITE, COLOR_BLACK);
	init_pair(1, COLOR_GREEN, COLOR_BLACK);
	init_pair(2, COLOR_YELLOW, COLOR_BLACK);
	attrset(A_BOLD);	// boldy
	curs_set(0);		// don't show cursor
#endif

	if (!nodownload && started > 0) {
		std::clog << "Downloading torrents..." << std::endl;
		while (total_bits ^ (completed | errors)) {
			for (size_t i = 0; i < total; ++i) {
				if (errors & (1 << i))
					continue;

				Torrent *t = &torrents[i];
				if (t->isFinished()) {
					if (!noseed)
						t->finish();
					completed |= 1 << i;
				} else {
					if (max_peers == 0 || t->activePeers() < max_peers)
						t->checkTrackers();
					if (!noseed)
						t->nextConnection();
				}
			}

			Connection::poll();
			print_all_stats(&torrents[0], total);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}

	std::clog << "\nDone downloading\n" << std::endl;
	if (!noseed && completed > 0) {
		std::clog << "Now seeding" << std::endl;
		for (size_t i = 0; i < total; ++i) {
			Torrent *t = &torrents[i];
			if (!t->hasTrackers())
				eseed |= 1 << i;
		}

		while (eseed ^ total_bits) {
			for (size_t i = 0; i < total; ++i) {
				Torrent *t = &torrents[i];
				if (!t->nextConnection() || (t->activePeers() < max_peers && !t->checkTrackers()))
					eseed |= 1 << i;
			}

			Connection::poll();
			print_all_stats(&torrents[0], total);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}

#ifndef _WIN32
	endwin();
#endif

	for (size_t i = 0; i < total; ++i) {
		Torrent *t = &torrents[i];
		const TorrentMeta *meta = t->meta();	
		if (completed & (1 << i))
			std::clog << "Completed: ";
		else if (errors & (1 << i))
			std::clog << "Something went wrong downloading: ";
		else if (eseed & (1 << i))
			std::clog << "Failed to seed: ";
		std::clog << meta->name() << std::endl;
	}

	std::clog << "Finished" << std::endl;
	logfile.close();
	return 0;
}


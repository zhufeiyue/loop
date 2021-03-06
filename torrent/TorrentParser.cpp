#include "TorrentParser.h"
#include <fstream>
#include <iomanip>
#include <memory>
#include <boost/uuid/detail/sha1.hpp>

using namespace boost;

BCodeInfoHash::BCodeInfoHash()
{
	m_bInfoHash = false;
}

void BCodeInfoHash::CreateInfoHash(const std::string& key, const char* data, int len)
{
	if (key != "info")
	{
		return;
	}
	if (!data || len < 10)
	{
		return;
	}

	unsigned int res[5] = { 0 };
	boost::uuids::detail::sha1 sha;
	sha.process_bytes(data, len);
	sha.get_digest(res);

	for (int i = 0; i < 5; ++i)
	{
		auto p = (int8_t*)&res[i];
		m_infoHash[i * 4] = p[3];
		m_infoHash[i * 4 + 1] = p[2];
		m_infoHash[i * 4 + 2] = p[1];
		m_infoHash[i * 4 + 3] = p[0];
	}
	m_bInfoHash = true;
}


int TorrentParser::ParseFromFile(std::string strFile)
{
	int res(-1);
	std::ifstream fileIn;
	std::streampos fileSize = 0;

	fileIn.open(strFile, std::ifstream::in | std::ifstream::binary);
	if (!fileIn.is_open())
	{
		return res;
	}

	fileIn.seekg(0, std::ifstream::end);
	fileSize = fileIn.tellg();
	fileIn.seekg(0, std::ifstream::beg);
	std::unique_ptr<char[]> pData(new char[(int)fileSize + 10]);
	if (!pData)
	{
		return res;
	}

	fileIn.read(pData.get(), (int)fileSize);
	auto readed = fileIn.gcount();
	if (readed != fileSize)
	{
		return res;
	}
	if (0 > m_bcode.Parse(pData.get(), (int)fileSize))
	{
		return res;
	}
	res = 1;
	return res;
}

void TorrentParser::ClearParse()
{
	m_bcode.Clear();
}

const BCodeInfoHash& TorrentParser::GetDictionary()const
{
	return m_bcode;
}

std::string TorrentParser::GetInfoHash()const
{
	if (m_bcode.m_bInfoHash)
	{
		return std::string((char*)m_bcode.m_infoHash, 20);
	}
	else
	{
		return "";
	}
}

std::vector<std::string> TorrentParser::GetTrackerURLs() const
{
	std::vector<std::string> urls;
	auto & dic = GetDictionary();
	auto announce = static_cast<const BCode_s*>(dic.GetValue("announce", BCode::string));
	if (announce)
	{
		urls.push_back(announce->m_str);
	}

	auto announce_list = static_cast<const BCode_l*>(dic.GetValue("announce-list", BCode::list));
	if (announce_list)
	{
		for (auto iter = announce_list->m_list.begin(); iter != announce_list->m_list.end(); ++iter)
		{
			auto item = static_cast<const BCode_l*>(*iter);
			if (item && item->m_list.size() > 0 && item->m_list[0]->GetType() == BCode::string)
			{
				urls.push_back(static_cast<const BCode_s*>(item->m_list[0])->m_str);
			}
		}
	}

	return urls;
}

int64_t TorrentParser::GetTotalSize()const
{
	auto fileInfos = GetFileInfo();
	int64_t res = 0;

	for (auto i : fileInfos)
	{
		res += i.size;
	}

	return res;
}

std::vector<TorrentParser::DownloadFileInfo> TorrentParser::GetFileInfo() const
{
	std::vector<TorrentParser::DownloadFileInfo> items;
	auto& dic = GetDictionary();

	if (!dic.Contain("info", BCode::dictionary))
	{
		return items;
	}
	auto info = static_cast<const BCode_d*>(dic.GetValue("info"));

	/*
	piece length
	pieces
	private

	1. single file
	name
	length
	md5sum

	2. multiple file
	name
	files
		length
		md5sum
		path
	*/

	if (info->Contain("length", BCode::interger))
	{
		DownloadFileInfo fileInfo;
		auto length = static_cast<const BCode_i*>(info->GetValue("length", BCode::interger));
		auto name = static_cast<const BCode_s*>(info->GetValue("name", BCode::string));
		auto md5 = static_cast<const BCode_s*>(info->GetValue("md5sum", BCode::string));

		if (length)
		{
			fileInfo.size = length->m_i;
		}
		if (name)
		{
			fileInfo.path = name->m_str;
		}
		if (md5)
		{
			fileInfo.md5 = md5->m_str;
		}

		items.push_back(fileInfo);
	}
	else if (info->Contain("files", BCode::list))
	{
		auto files = static_cast<const BCode_l*>(info->GetValue("files", BCode::list));
		for (auto iter = files->m_list.begin();
			iter != files->m_list.end();
			++iter)
		{
			auto item = *iter;
			if (item->GetType() == BCode::dictionary)
			{
				auto item_dic = static_cast<const BCode_d*>(item);
				if (item_dic->Contain("length", BCode::interger))
				{
					auto length = static_cast<const BCode_i*>(item_dic->GetValue("length", BCode::interger));
					auto path = static_cast<const BCode_l*>(item_dic->GetValue("path", BCode::list));
					auto md5 = static_cast<const BCode_s*>(item_dic->GetValue("md5sum", BCode::string));

					if (length && path && path->m_list.size() > 0)
					{
						DownloadFileInfo fileInfo;
						fileInfo.size = length->m_i;
						fileInfo.path = static_cast<const BCode_s*>(path->m_list[0])->m_str;
						if (md5)
						{
							fileInfo.md5 = md5->m_str;
						}
						items.push_back(fileInfo);
					}
				}
			}
			else
			{
			}
		}
	}

	return items;
}

int32_t TorrentParser::GetPieceSize()const
{

	auto &dic = GetDictionary();
	if (!dic.Contain("info", BCode::dictionary))
	{
		return 0;
	}
	auto info = static_cast<const BCode_d*>(dic.GetValue("info"));

	if (info->Contain("piece length", BCode::interger))
	{
		auto pl = static_cast<const BCode_i*>(info->GetValue("piece length"));
		return (int32_t)pl->m_i;
	}
	else
	{
		return 0;
	}
}

int32_t TorrentParser::GetPieceNumber()const
{
	auto &dic = GetDictionary();
	if (!dic.Contain("info", BCode::dictionary))
	{
		return 0;
	}
	auto info = static_cast<const BCode_d*>(dic.GetValue("info"));

	if (info->Contain("pieces", BCode::string))
	{
		auto p = static_cast<const BCode_s*>(info->GetValue("pieces"));
		auto pieces_size = p->m_str.length();

		return int32_t(pieces_size) / 20;
	}
	else
	{
		return 0;
	}
}


int TorrentFile::LoadFromFile(std::string s)
{
	m_torrentFileParser.ClearParse();

	int res = m_torrentFileParser.ParseFromFile(s);
	if (res < 0)
	{
		return res;
	}

	auto fileInfos = m_torrentFileParser.GetFileInfo();
	auto totalSize = m_torrentFileParser.GetTotalSize();
	auto pieceNum = m_torrentFileParser.GetPieceNumber();
	auto pieceSize = m_torrentFileParser.GetPieceSize();
	auto trackerURLs = m_torrentFileParser.GetTrackerURLs();
	auto infoHash = m_torrentFileParser.GetInfoHash();

	if (trackerURLs.empty() ||
		infoHash.length() != 20)
	{
		return -1;
	}

	return 0;
}
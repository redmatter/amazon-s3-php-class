#ifndef __SYNCSERVER_H__
#define __SYNCSERVER_H__

#include <boost/asio.hpp>
#include <boost/thread/recursive_mutex.hpp>

typedef boost::tokenizer<boost::char_separator<char> > char_tokenizer;
#define FOREACHCONST(type,container,iter) for (type::const_iterator iter = container.begin(); iter != container.end(); ++iter)

class SyncServer
{
	typedef boost::shared_ptr<boost::asio::deadline_timer> TIMER;

public:

	SyncServer(
			boost::asio::io_service &io_service,
			std::string partnerhost,
			unsigned short port,
			archiving::archivable_channel_t& in_channel,
			archiving::archivable_channel_t& out_channel
	) : io_service_(io_service), in_channel_(in_channel), out_channel_(out_channel),
		localendpoint_(boost::asio::ip::udp::v4(), port),
		localsocket_(io_service_, localendpoint_)
	{
		try
		{
			boost::asio::ip::udp::resolver resolver(io_service);
			boost::asio::ip::udp::resolver::query query(partnerhost, "0");
			boost::asio::ip::udp::resolver::iterator endpoint_iterator = resolver.resolve(query);
			if (endpoint_iterator != boost::asio::ip::udp::resolver::iterator())
			{
				remote_endpoint_ = *endpoint_iterator;
				remote_endpoint_.port(port);
				LogVerbose("SyncServer(): " << partnerhost << " resolves to " << remote_endpoint_.address());
			}
			else
			{
				LogError("SyncServer(): Unable to resolve sync host partner: " << partnerhost);
				throw new std::exception;
			}
		}
		catch(std::exception &e)
		{
			LogError("SyncServer(): exception resolving sync host partner: " << partnerhost << ": " << e.what());
			throw new std::exception;
		}

		Listen();

		threads_.create_thread(boost::bind(&SyncServer::run, this));
	}

	void Release(const std::string &uuid)
	{
		boost::recursive_mutex::scoped_lock lock(mutex_);

		items_.erase(uuid);
		timers_.erase(uuid);
		we_own_.erase(uuid);
	}

private:

	void Handle(bool handle, const std::string &uuid, bool in_timer = false)
	{
		boost::recursive_mutex::scoped_lock lock(mutex_);

		std::map<std::string, archiving::archivable::pointer>::iterator i = items_.find(uuid);
		if (i == items_.end())
		{
			LogError("SyncServer::Handle(" << uuid << "): no matching item");
		}
		else
		{
			std::map<std::string, TIMER>::iterator t = timers_.find(uuid);
			if (t == timers_.end())
				LogMessage("SyncServer::Handle(" << uuid << "): timer already completed, assuming handled");
			else
			{
				if(!in_timer)
					t->second->cancel();

				timers_.erase(t);

				if (handle)
				{
					LogMessage("SyncServer::Handle(" << uuid << "): handling");
					we_own_.insert(uuid);
					out_channel_.produce(i->second);
				}
				else
					LogMessage("SyncServer::Handle(" << uuid << "): not handling");

				items_.erase(uuid);
			}
		}
	}

	void Listen()
	{
		localsocket_.async_receive_from(
			boost::asio::buffer(listen_buff_, sizeof(listen_buff_)),
		    remote_endpoint_,
		    boost::bind(&SyncServer::handle_Receive,
				this,
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
	}

	void handle_Receive(const boost::system::error_code& error, size_t bytes_transferred)
	{
		if (!error)
		{
			std::string listen(listen_buff_, bytes_transferred);

			char_tokenizer messages(listen, boost::char_separator<char>("\n"));
			FOREACHCONST(char_tokenizer, messages, imsg)
			{
				LogMessage("SyncServer::handle_Receive: " << listen);

				std::string opcode, param;
				size_t space = imsg->find(" ");
				if ((space != std::string::npos) && (space > 0) && (space < (imsg->length() - 1)))
				{
					opcode = imsg->substr(0, space);
					param = imsg->substr(space + 1);

					if (opcode == "GET")
					{
						boost::recursive_mutex::scoped_lock lock(mutex_);

						if (we_own_.find(param) != we_own_.end())
							Reply(std::string("MINE ") + param + "\n");
						else if (items_.find(param) != items_.end())
						{
							std::ostringstream os;
							os << remote_endpoint_.address();
							Reply(std::string("NEGOTIATE ") + param + " " + os.str() + "\n");
						}
						else
							Reply(std::string("YOURS ") + param + "\n");
					}
					else if (opcode == "MINE")
					{
						LogMessage("SyncServer::handle_Receive: partner is handling " << param);
						Handle(false, param);
					}
					else if (opcode == "YOURS")
					{
						LogMessage("SyncServer::handle_Receive: we are handling " << param);
						Handle(true, param);
					}
					else if (opcode == "NEGOTIATE")
					{
						// we are both trying to handle the same uuid. The host
						// who's IP is lexigraphically lower wins.

						std::string uuid, ip;
						size_t space = param.find(" ");
						if ((space != std::string::npos) && (space > 0) && (space < (param.length() - 1)))
						{
							uuid = param.substr(0, space);
							ip = param.substr(space + 1);

							std::ostringstream os;
							os << remote_endpoint_.address();

							LogMessage("SyncServer::handle_Receive: negotiate " << uuid << ": local ip " << ip << ", remote ip " << os.str());

							if (ip <= os.str())
							{
								Reply(std::string("MINE ") + uuid + "\n");
								Handle(true, uuid);
							}
							else
							{
								Reply(std::string("YOURS ") + uuid + "\n");
								Handle(false, uuid);
							}
						}
						else
							LogWarning("SyncServer::handle_Receive: negotiate: invalid command string");
					}
				}
				else
				{
					LogWarning("SyncServer::handle_Receive: incorrectly formatted string from sync partner: " << *imsg);
				}
			}
		}
		else
			LogWarning("SyncServer::handle_Receive: error receiving data: " << error.message());

		Listen();
	}

	void Reply(const std::string &reply)
	{
		std::string *buff = new std::string(reply);

		localsocket_.async_send_to(
		    boost::asio::buffer(*buff),
		    remote_endpoint_,
		    boost::bind(&SyncServer::handle_Reply,
				this,
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred,
				buff, reply));
	}

	void handle_Reply(const boost::system::error_code& error, size_t bytes_transferred, std::string *buff, const std::string reply)
	{
		if (!error)
			LogVerbose("SyncServer::handle_Reply(): " << reply << " sent ok");
		else
			LogMessage("SyncServer::handle_Reply(): " << reply << ": error sending message: " << error.message());

		delete buff;
	}

	void Get(const std::string uuid)
	{
		std::string *buff = new std::string("GET " + uuid + "\n");

		localsocket_.async_send_to(
		    boost::asio::buffer(*buff),
		    remote_endpoint_,
		    boost::bind(&SyncServer::handle_Get,
				this,
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred,
				buff, uuid));
	}

	void handle_Get(const boost::system::error_code& error, size_t bytes_transferred, std::string *buff, const std::string uuid)
	{
		delete buff;

		if (!error)
		{
			TIMER timer = TIMER(new boost::asio::deadline_timer(io_service_));

			timer->expires_from_now(boost::posix_time::seconds(2));
			timer->async_wait(
					boost::bind(&SyncServer::handle_Timer,
									this,
									boost::asio::placeholders::error,
									uuid));

			mutex_.lock();
			timers_.insert(std::make_pair(uuid, timer));
			mutex_.unlock();
		}
		else
		{
			LogMessage("SyncServer::handle_Get(): error sending message: " << error.message() << ": " << " going ahead and processing " << uuid);
			Handle(true, uuid);
		}
	}

	void handle_Timer (const boost::system::error_code& error, const std::string uuid)
	{
		// we process the item unless the timer is cancelled, in which case the thing doing the cancelling
		// does the necessary, including deleting the timer

		if (error == boost::asio::error::operation_aborted)
			LogMessage("SyncServer::handle_Timer(): " << uuid << ": timer cancelled.");
		else
		{
			if (!error)
				LogMessage("SyncServer::handle_Timer(): " << uuid << ": timed out waiting for response from partner.");
			else
				LogMessage("SyncServer::handle_Timer(): " << uuid << ": timer error: " << error.message());

			Handle(true, uuid, true);
		}
	}

	void run()
	{
		while (!app::quit)
		{
			try
			{
				archiving::archivable::pointer item = in_channel_.consume();

				LogVerbose("SyncServer: querying " << item->get_uuid());

				mutex_.lock();
				items_.insert(std::make_pair(item->get_uuid(), item));
				mutex_.unlock();

				Get(item->get_uuid());
			}
			catch (const std::exception& e)
			{
				LogWarning("exception:" << e.what());
			}
			catch (...)
			{
				LogCritical("unexpected exception");
			}
		}
	}

	boost::thread_group threads_;

	boost::asio::io_service &io_service_;
	archiving::archivable_channel_t& in_channel_;
	archiving::archivable_channel_t& out_channel_;

	std::set<std::string> we_own_;
	std::map<std::string, TIMER> timers_;
	std::map<std::string, archiving::archivable::pointer> items_;

	boost::asio::ip::udp::endpoint remote_endpoint_;
	boost::asio::ip::udp::endpoint localendpoint_;
	boost::asio::ip::udp::socket localsocket_;
	char listen_buff_[1024];

	boost::recursive_mutex mutex_;
};

#endif //__SYNCSERVER_H__

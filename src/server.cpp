// TODO change config.hpp and chat_messages.hpp to correct path

#include "config.hpp"
#include "chat_config.hpp"
#include "chat_messages.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <memory>
#include <utility>
#include <list>


using boost::asio::ip::tcp;


void remove_session(int id);
void broadcast(char * data);
void broadcast_connected(int id);
void broadcast_disconnected(int id);
std::string who_is_connected();
bool send_message(int dst, const char * data);


class session
    : public std::enable_shared_from_this<session>
{
    public:
        session(tcp::socket sock, int id)
            : sock_(std::move(sock))
        {
            id_ = id;
        }

        void start()
        {
            response_who_is_connected();
            
            do_read();
        }

        void close_socket()
        {
            sock_.close();
        }

        void response(const char * data, std::size_t length)
        {
            auto self(shared_from_this());

            boost::asio::async_write(sock_, boost::asio::buffer(data, length),
                [this, self](boost::system::error_code ec, std::size_t length)
            {
                if (ec == boost::asio::error::eof)
                {
                    handle_disconnect();
                }    
            });
        }

        void response_full_lobby(const char * data, std::size_t length)
        {
            auto self(shared_from_this());

            boost::system::error_code ec;

            sock_.write_some(boost::asio::buffer(data, length), ec);

            if (ec != boost::asio::error::eof && ec)
            {
                throw boost::system::system_error(ec);
            }
        }

        void response_who_is_connected()
        {
            const char * data;
            std::string message;

            message = who_is_connected();
            
            data = message.c_str();

            response(data, strlen(data));
        }


    private:
        void do_read()
        {
            auto self(shared_from_this());
            sock_.async_read_some(boost::asio::buffer(data_, DATA_MAX_LENGTH),
                [this, self](boost::system::error_code ec, std::size_t length)
            {
                if (ec == boost::asio::error::eof)
                {
                    handle_disconnect();
                }

                else if (!ec)
                {
                    handle_read(length); 
                }
            });
        }

        void handle_read(std::size_t length)
        {
            std::string data_str;
            std::string response_message;
            const char * response_data;

            int dst;
            std::string message;

            data_str = data_;

            data_str += "\n";

            dst = get_dst(data_str);

            message = std::string(FROM_WHO_MESSAGE) + std::to_string(id_) + std::string(COLON); 
            message += get_message(data_str);

            if (send_message(dst, message.c_str()))
            {
                response_message = std::string(SUCCESS_SEND_MESSAGE);

                response_data = response_message.c_str();

                response(response_data, strlen(response_data));
            }

            else
            {
                response_message = std::string(ERROR_SEND_MESSAGE);

                response_data = response_message.c_str();

                response(response_data, strlen(response_data));
            }

            do_read();
        }

        void handle_disconnect()
        {
            close_socket();
            remove_session(id_);
            broadcast_disconnected(id_);
        }

        int get_dst(std::string data)
        {
            return std::stoi(data.substr(0, data.find(SPACE)));
        }

        std::string get_message(std::string data)
        {
            return data.substr(data.find(SPACE) + 1);
        }

        tcp::socket sock_;
        char data_[DATA_MAX_LENGTH];
        int id_;
};


std::pair<std::shared_ptr<session>, int> * find(int id);
bool send(std::shared_ptr<session> * s, const char * data);

static int client_id = 1;

std::list<std::pair <std::shared_ptr<session>, int> > sessions;

class server
{
    public:
        server(boost::asio::io_service& io_service, short port)
            : acceptor_(io_service, tcp::endpoint(tcp::v4(), port)),
            sock_(io_service)
        {
            do_accept();
        }

    private:
        void do_accept()
        {
            acceptor_.async_accept(sock_,
                [this](boost::system::error_code ec)
            {
                if (!ec)
                {
                    std::pair <std::shared_ptr<session>, int> s;
                    s = std::make_pair(std::make_shared<session>(std::move(sock_), client_id),
                        client_id);

                    if (sessions.size() >= MAX_CLIENTS)
                    {
                        const char * data;

                        data = ERROR_CONNECT_LOBBY_FULL_MESSAGE;

                        s.first->response_full_lobby(data, strlen(data));

                        s.first->close_socket();
                    }

                    else
                    {
                        std::cout << client_id << SPACE << CONNECTED_MESSAGE;

                        broadcast_connected(client_id);

                        client_id++;

                        s.first->start();
     
                        sessions.push_back(s);                   
                    }
                }

                do_accept();
            });
        }

        tcp::acceptor acceptor_;
        tcp::socket sock_;
};


void broadcast(const char * data)
{
    std::size_t length;
 
    length = strlen(data);

    for(auto s:sessions)
    {
        s.first->response(data, length);
    }
}


void broadcast_connected(int id)
{
    std::string message;

    message = std::to_string(id) + std::string(SPACE) +
        std::string(CONNECTED_MESSAGE);

    broadcast(message.c_str());
}

void broadcast_disconnected(int id)
{
    std::string message;

    message = std::to_string(id) + std::string(SPACE) +
        std::string(DISCONNECTED_MESSAGE);

    broadcast(message.c_str());
}


void remove_session(int id)
{
    for(auto s = sessions.begin(); s != sessions.end(); s++)
    {
        if ((*s).second == id)
        {
            s = sessions.erase(s);
            std::cout << id << SPACE << DISCONNECTED_MESSAGE << std::endl;
        }
    }
}


std::string who_is_connected()
{
    std::string message(CONNECTIONS_MESSAGE);

    for(auto s = sessions.begin(); s != sessions.end(); s++)
    {
        message += std::string(ASTERISK) + std::to_string((*s).second) + std::string("\n");
    }

    return message;
}



std::pair<std::shared_ptr<session>, int> * find(int id)
{
    for(auto s = sessions.begin(); s != sessions.end(); s++)
    {
        if ((*s).second == id)
        {
            return &((*s));
        }
    }

    return nullptr;
}


bool send(std::pair<std::shared_ptr<session>, int> * s, const char * data)
{
    try
    {
        ((*s).first)->response(data, strlen(data));

        return true;
    }

    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;

        return false;
    }

}

bool send_message(int dst, const char * data)
{
    std::pair<std::shared_ptr<session>, int> * s;

    s = find(dst);

    if(!s)
    {
        return false;
    }

    return send(s, data);
}


int main(int argc, char* argv[])
{
    try
    {
        boost::asio::io_service io_service;


        server s(io_service, PORT);

        io_service.run();
    }

    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}
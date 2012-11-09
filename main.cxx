#include <ctime>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <string>
#include <thread>
#include <exception>
#include <stdexcept>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem/operations.hpp>

using boost::asio::ip::tcp;

const std::string http_version = "HTTP/1.0";
const std::string server_name = "crash";

static std::string rfc1123_datetime(time_t time)
{
    struct tm* timeinfo;
    char buffer[80];

    timeinfo = gmtime(&time);
    strftime(buffer, 80, "%a, %d %b %Y %H:%M:%S GMT", timeinfo);

    return buffer;
}

class tcp_connection
    : public boost::enable_shared_from_this<tcp_connection>
{
public:
    typedef boost::shared_ptr<tcp_connection> pointer;

    static pointer create(boost::asio::io_service& io_service)
    {
        return pointer(new tcp_connection(io_service));
    }

    tcp::socket& socket()
    {
        return socket_;
    }

    void start()
    {
        boost::asio::streambuf b;
        std::istream is(&b);
        std::string single_command;

        try
        {
            for(;;)
            {
                boost::asio::read_until(socket_, b, '\n');
                std::getline(is, single_command);
                if(single_command == "\r")
                    break;
                else if(boost::to_upper_copy(single_command.substr(0, 4)) == "GET ")
                {
                    std::string what_to_get = single_command.substr(4);
                    _get = what_to_get.substr(0, what_to_get.find_first_of(' '));
                    if(*(_get.rbegin()) == '\r')
                    {
                        _get = _get.substr(0, _get.size() - 1);
                    }
                }
                else if(boost::to_upper_copy(single_command.substr(0, 18)) == "IF-MODIFIED-SINCE:")
                {
                    _if_modified_since = single_command.substr(18);
                }
                else
                {
                    continue;
                }
            }
            std::string reply;
            std::vector<char> final_reply;
            FILE* in = std::fopen(_get.c_str(), "r");
            if(in == NULL)
            {
                reply = http_version + " 404 Not Found\r\n\r\nNot Found\r\n";
            }
            else
            {
                // check modification
                std::time_t on_disk_time = boost::filesystem::last_write_time(boost::filesystem::path(_get));
                struct tm request_time;
                if(strptime(_if_modified_since.c_str(), " %a, %d %b %Y %H:%M:%S %Z", &request_time))
                {
                    std::time_t request_time_t = timegm(&request_time); // -1 on fail
                    if(on_disk_time <= request_time_t)
                    {
                        reply = http_version + " 304 Not Modified\r\n";
                        reply += "Date: " + rfc1123_datetime(time(nullptr)) + "\r\n";
                        reply += "\r\n";
                        boost::asio::write(socket_, boost::asio::buffer(reply));
                        return;
                    }
                }

                // we want to send new version
                reply = http_version + " 200 OK\r\n";
                reply += "Date: " + rfc1123_datetime(time(nullptr)) + "\r\n";
                reply += "Server: " + server_name + "\r\n";
                reply += "Last-Modified: " + rfc1123_datetime(on_disk_time) + "\r\n";
                std::string extension = boost::to_upper_copy(_get.substr(_get.find_last_of('.') + 1));
                bool text = false;
                bool image = false;
                if(extension == "TXT")
                {
                    reply += "Content-Type: text/plain\r\n";
                    text = true;
                }
                else if(extension == "HTM" || extension == "HTML")
                {
                    reply += "Content-Type: text/html\r\n";
                    text = true;
                }
                else if(extension == "JPG" || extension == "JPEG")
                {
                    reply += "Content-Type: image/jpeg\r\n";
                    image = true;
                }
                else if(extension == "GIF")
                {
                    reply += "Content-Type: image/gif\r\n";
                    image = true;
                }
                else if(extension == "CLASS")
                {
                    reply += "Content-Type: application/java-vm\r\n";
                    image = true;
                }
                else
                {
                    reply = http_version + " 501 Not Implemented\r\n\r\nSorry this feature is not implemented.\r\n";
                }
                if(text)
                {
                    reply += "\r\n";
                    std::fseek(in, 0, SEEK_END);
                    long size = std::ftell(in);
                    std::rewind(in);
                    char* buff = new char[size + 1];
                    std::fread(buff, 1, size, in);
                    buff[size] = 0;
                    reply += buff;
                    delete [] buff;
                }
                if(image)
                {
                    std::fseek(in, 0, SEEK_END);
                    long size = std::ftell(in);
                    std::rewind(in);
                    char* buff = new char[size];
                    std::fread(buff, 1, size, in);
                    reply += "Content-Length: " + std::to_string(size) + "\r\n";
                    reply += "\r\n";
                    final_reply.insert(final_reply.begin(), reply.begin(), reply.end());
                    final_reply.insert(final_reply.end(), buff, buff + size);
                    delete [] buff;
                }
            }
            if(final_reply.empty())
                boost::asio::write(socket_, boost::asio::buffer(reply));
            else
            {
                boost::asio::write(socket_, boost::asio::buffer(final_reply));
            }
        }
        catch(const std::exception& ignored)
        {
            // Let this thread end

            // Some kind of web browser will open new connection in response to
            // closing this connection (bacause we have HTTP/1.0, but client may
            // expect HTTP/1.1). The request in new connection will be malformed
            // and cause this exception.
        }
    }

private:
    tcp_connection(boost::asio::io_service& io_service)
        : socket_(io_service)
    {
    }

    std::string _get;
    std::string _if_modified_since;
    tcp::socket socket_;
};

class tcp_server
{
public:
    tcp_server(boost::asio::io_service& io_service, std::uint16_t port)
        : acceptor_(io_service, tcp::endpoint(tcp::v4(), port))
    {
        start_accept();
    }

private:
    void start_accept()
    {
        for(;;)
        {
            tcp_connection::pointer new_connection =
                tcp_connection::create(acceptor_.get_io_service());
            acceptor_.accept(new_connection->socket());
            std::thread thread(&tcp_connection::start, new_connection);
            thread.detach();

            // In fact this technique is obsolete and it is possible to do it
            // better. But this is easier and satisfies our requirements.
        }
    }

    tcp::acceptor acceptor_;
};

int main()
{
    try
    {
        boost::asio::io_service io_service;
        tcp_server server(io_service, 8080);
    }
    catch(const std::exception& e)
    {
        std::cerr << "Serwer padł: " << e.what() << std::endl;
    }

    return 0;
}



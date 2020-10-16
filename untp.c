#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define NTP_VERSION 0x3
#define NTP_MODE_CLIENT 0x3
#define NTP_TO_UNIX_OFFSET 2208988800ull

// Official ref: https://tools.ietf.org/html/rfc5905
typedef struct __attribute__((packed))
{

    uint8_t leap_version_mode;
    uint8_t stratum;
    uint8_t poll;
    uint8_t precision;

    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t reference_id;

    uint32_t reference_sec; //Time when the system clock was last set or corrected, in NTP timestamp format.
    uint32_t reference_frac;

    uint32_t origin_sec; // Time at the client when the request departed for the server, in NTP timestamp format.
    uint32_t origin_frac;

    uint32_t receive_sec; //Time at the server when the request arrived from the client, in NTP timestamp format.
    uint32_t receive_frac;

    uint32_t transmit_sec; // Time at the server when the response left for the client, in NTP timestamp format.
    uint32_t transmit_frac;

} ntp_packet;

static inline uint64_t unix_to_ntp(struct timespec t)
{
    // https://tools.ietf.org/html/rfc5905 - page 13
    uint32_t result_secs, result_frac;
    result_secs = t.tv_sec + NTP_TO_UNIX_OFFSET;
    double frac = (double)t.tv_nsec / (double)1e9;
    result_frac = (uint32_t)(frac * (double)0xFFFFFFFF);

    uint64_t result = ((uint64_t)result_secs << 32) | (result_frac & 0xFFFFFFFF);
    return result;
}

static inline struct timespec ntp_to_unix(uint64_t ntp)
{
    uint32_t result_secs, result_frac;
    result_secs = (ntp >> 32) & 0xFFFFFFFF;
    result_frac = (ntp & 0xFFFFFFFF);

    struct timespec result;
    result.tv_sec = result_secs - NTP_TO_UNIX_OFFSET;
    double tmp = (double)result_frac / (double)0xFFFFFFFF;
    result.tv_nsec = (long)(tmp * (double)1e9);

    return result;
}

static inline struct timespec timespec_sub(struct timespec t1, struct timespec t2)
{
    struct timespec result;
    result.tv_sec = t1.tv_sec - t2.tv_sec;
    result.tv_nsec = t1.tv_nsec - t2.tv_nsec;
    if (result.tv_nsec < 0)
    {
        result.tv_sec--;
        result.tv_nsec += 1e9;
    }
    return result;
}

static inline struct timespec timespec_add(struct timespec t1, struct timespec t2)
{
    struct timespec result;
    result.tv_sec = t1.tv_sec + t2.tv_sec;
    result.tv_nsec = t1.tv_nsec + t2.tv_nsec;
    if (result.tv_nsec >= 1e9)
    {
        result.tv_sec++;
        result.tv_nsec -= 1e9;
    }
    return result;
}

void print_timespec(struct timespec t)
{

    printf("%s ", ctime((const time_t *)&t.tv_sec));
    printf("and %ld ns (%lf s)\n", t.tv_nsec, (double)t.tv_nsec / (double)1e9);
}

int main(int argc, char *argv[])
{
    // check usage
    if (argc != 2)
    {
        printf("untp: barebones NTP client\n");
        printf("   usage: untp ntp.example.com\n");
        exit(1);
    }

    ntp_packet request;
    request.leap_version_mode = 0x1B;

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
    {
        printf("error opening UDP socket\n");
        exit(1);
    }

    //  sender info
    struct sockaddr_in me;
    memset(&me, 0, sizeof(me));
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_ANY);
    me.sin_port = htons(0); // let os pick port

    // server info
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(123); // NTP port

    // get IP from hostname
    struct hostent *host_info = gethostbyname(argv[1]);
    if (host_info == NULL)
    {
        printf("no such host: %s\n", (char *)argv[1]);
        exit(1);
    }
    memcpy((void *)&serv.sin_addr, host_info->h_addr_list[0], host_info->h_length);

#if 0
    printf("available IP addresses for host %s (picking first):\n", HOST);
    for (int i = 0; host_info->h_addr_list[i] != 0; i++)
    {
        unsigned char *stuff = (unsigned char *)host_info->h_addr_list[i];
        printf("  %d) %d.%d.%d.%d\n", i + 1, stuff[0], stuff[1], stuff[2], stuff[3]);
    }
#endif

    // bind
    if (bind(s, (struct sockaddr *)&me, sizeof(me)) < 0)
    {
        printf("bind failed\n");
        exit(1);
    }

    // sending the packet: first, get the current time and fill the origin field of the NTP packet
    struct timespec ts_origin = {0, 0};
    clock_gettime(CLOCK_REALTIME, &ts_origin);
    uint64_t origin_ntp_timestamp = unix_to_ntp(ts_origin);
    request.origin_frac = origin_ntp_timestamp & 0xFFFFFFFF;
    request.origin_sec = (origin_ntp_timestamp >> 32) & 0xFFFFFFFF;
    // send the packet
    if (sendto(s, &request, sizeof(request), 0, (struct sockaddr *)&serv, sizeof(serv)) < 0)
    {
        printf("sendto failed\n");
        exit(1);
    }

    // recieve the response
    socklen_t tmp = sizeof(serv);
    int recvlen = recvfrom(s, &request, sizeof(request), 0, (struct sockaddr *)&serv, &tmp);
    struct timespec ts_dest = {0, 0};
    // note the time at which the response arrived
    clock_gettime(CLOCK_REALTIME, &ts_dest);
    printf("received %d bytes\n", recvlen);

    // swap endianness if necessary
    request.transmit_sec = ntohl(request.transmit_sec);
    request.transmit_frac = ntohl(request.transmit_frac);
    request.receive_sec = ntohl(request.receive_sec);
    request.receive_frac = ntohl(request.receive_frac);
    request.origin_sec = ntohl(request.origin_sec);
    request.origin_frac = ntohl(request.origin_frac);
    request.stratum = ntohl(request.stratum);

    // calculate rtt and offset
    struct timespec ts_receive = ntp_to_unix(((uint64_t)request.receive_sec << 32) | request.receive_frac);
    struct timespec ts_transmit = ntp_to_unix(((uint64_t)request.transmit_sec << 32) | request.transmit_frac);
    struct timespec rtt = timespec_sub(timespec_sub(ts_dest, ts_origin), timespec_sub(ts_transmit, ts_receive));
    struct timespec offset = timespec_add(timespec_sub(ts_receive, ts_origin), timespec_sub(ts_transmit, ts_dest));
    offset.tv_nsec /= 2;
    offset.tv_sec /= 2;

    // TODO:(sbrki) Intersection algorithm (https://en.wikipedia.org/wiki/Intersection_algorithm)

    // print summary
    printf("stratum: %d\n", request.stratum);
    printf("rtt: %ld sec %ld ns (%lf s)\n", rtt.tv_sec, rtt.tv_nsec, (double)rtt.tv_nsec / (double)1e9);
    printf("client and server time are offset by: %ld sec and %ld ns (%lf s)\n", offset.tv_sec, offset.tv_nsec, (double)offset.tv_nsec / (double)1e9);
    printf("server transmit time: ");
    print_timespec(ts_transmit);

    return 0;
}
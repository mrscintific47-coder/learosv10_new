#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QPainter>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/types.h>

struct IPCChannel {
    enum Type { Pipe, SharedMem, UnixSocket };
    Type        type;
    std::string name;
    pid_t       senderPid   = -1;
    pid_t       receiverPid = -1;
    int         bytesSent   = 0;
    int         bytesRecv   = 0;
    bool        alive       = false;
    int         pipeFds[2]  = {-1,-1};
    int         shmId       = -1;
    void*       shmPtr      = nullptr;
    int         shmSize     = 0;
    int         serverFd    = -1;
    int         clientFd    = -1;
    std::string socketPath;

    // Message framing: every Send prefixes the payload with a 4-byte length
    // header so discrete Sends stay discrete on Read, even though the
    // underlying pipe/socket is just a raw byte stream. rawRecvBuffer holds
    // whatever undecoded bytes have been pulled off the fd so far; Read()
    // peels off at most ONE complete frame per click and leaves the rest
    // buffered for the next click.
    std::string rawRecvBuffer;
};

class IPCFlowView : public QWidget {
    Q_OBJECT

public:
    explicit IPCFlowView(QWidget* parent = nullptr);
    void setChannels(const std::vector<IPCChannel>& channels);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<IPCChannel> channels;
    void drawFlowArrow(QPainter& p, QPoint from, QPoint to, QColor color, int bytes);
};

class IPCLab : public QWidget {
    Q_OBJECT

public:
    explicit IPCLab(QWidget* parent = nullptr);
    ~IPCLab();

signals:
    void explanationNeeded(QString text);

private slots:
    void createPipe();
    void createSharedMem();
    void createSocket();
    void sendData();
    void readData();
    void destroySelected();
    void onRefresh();
    void onChannelSelected(int row, int col);

private:
    IPCFlowView*  flowView;
    QTableWidget* channelTable;
    QLineEdit*    dataInput;
    QLabel*       statusLabel;
    QTextEdit*    dataLog;
    QTimer*       refreshTimer;

    std::vector<IPCChannel> channels;
    int selectedChannel = -1;

    struct WorkerPair { pid_t sender=-1; pid_t receiver=-1; };
    std::vector<WorkerPair> workers;

    void refreshTable();
    void killChannel(int idx);
    void explainChannel(const IPCChannel& ch);
};

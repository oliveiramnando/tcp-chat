#include <termios.h>
#include <stdio.h>
#include <iostream>
#include <unistd.h>

using namespace std;

int main()
{
    termios terminal{};

    tcgetattr(STDIN_FILENO, &terminal);

    termios modified = terminal;
    modified.c_lflag &= ~(ECHO | ICANON);   // disable echo and icanon 
    modified.c_cc[VMIN] = 1;

    tcsetattr(STDIN_FILENO, TCSANOW, &modified);

    string message;
    char ch;

    cout << "You : " << flush;

    while (read(STDIN_FILENO, &ch, 1) == 1)
    {
        // enter
        if (ch == '\n' || ch == '\r')
        {
            if (message == "q()")
            {
                cout << "\r\033[2K";
                cout << "Exiting program..." << endl;
                break;
            }

            cout << "\r\033[2K";
            cout << "Sent : " << message << flush;

            message.clear();

            cout << "\nYou : " << flush;
            continue;
        }

        // backspace
        if (ch == 127 || ch == '\b')
        {
            if (!message.empty())
            { 
                message.pop_back();
            }
        }
        // char
        else if (ch >= 32 && ch <= 126)
        {
            message+= ch;
        }

        cout << "\r\033[2K";
        cout << "You : " << message << flush;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &terminal);

    cout << "Terminal Restored." << endl;

    return 0;
}


// Copyright (C) 2024 The Qt Company Ltd.
// Copyright (C) 2024 Robert Griebl
// SPDX-License-Identifier: BSD-3-Clause

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QResource>
#include <QLibrary>
#include <QDirIterator>

#if defined(Q_OS_WINDOWS)
#  include <io.h>
#  define write _write
#else
#  include <unistd.h>
#endif

using namespace Qt::StringLiterals;

bool loadResource(const QString &resource) noexcept(false)
{
    QString afp = QDir().absoluteFilePath(resource);
    QStringList errors;

    if (QResource::registerResource(resource))
        return true;
    errors.append(u"Cannot load as Qt Resource file"_s);

    QLibrary lib(afp);
    if (lib.load())
        return true;
    errors.append(lib.errorString());

    QString err = u"Failed to load resource %1:\n  * %2"_s
                      .arg(resource).arg(errors.join(u"\n  * "_s));
    fprintf(stderr, "%s\n", qPrintable(err));
    return false;
}

enum Command {
    NoCommand,
    Verify,
    List,
    Cat,
};

// REMEMBER to update the completion file util/bash/appman-prompt, if you apply changes below!
static struct {
    Command command;
    const char *name;
    const char *description;
} commandTable[] = {
    { Verify, "verify", "Verify a Qt resource file or library." },
    { List,   "list",   "List the files of a Qt resource file or library." },
    { Cat,    "cat",    "Cat a file from a  Qt resource file or library." },
};

static Command command(QCommandLineParser &clp)
{
    if (!clp.positionalArguments().isEmpty()) {
        QByteArray cmd = clp.positionalArguments().at(0).toLatin1();

        for (uint i = 0; i < sizeof(commandTable) / sizeof(commandTable[0]); ++i) {
            if (cmd == commandTable[i].name) {
                clp.clearPositionalArguments();
                clp.addPositionalArgument(QString::fromLatin1(cmd),
                                          QString::fromLatin1(commandTable[i].description),
                                          QString::fromLatin1(cmd));
                return commandTable[i].command;
            }
        }
    }
    return NoCommand;
}

int main(int argc, char *argv[])
{
    QCoreApplication::setApplicationName(u"Qt Resources Tool"_s);
    QCoreApplication::setOrganizationName(u"QtProject"_s);
    QCoreApplication::setOrganizationDomain(u"qt-project.org"_s);
    QCoreApplication::setApplicationVersion(u"1.0"_s);

    QCoreApplication a(argc, argv);

    QByteArray desc = "\n\nAvailable commands are:\n";
    size_t longestName = 0;
    for (uint i = 0; i < sizeof(commandTable) / sizeof(commandTable[0]); ++i)
        longestName = qMax(longestName, qstrlen(commandTable[i].name));
    for (uint i = 0; i < sizeof(commandTable) / sizeof(commandTable[0]); ++i) {
        desc += "  ";
        desc += commandTable[i].name;
        desc += QByteArray(1 + qsizetype(longestName - qstrlen(commandTable[i].name)), ' ');
        desc += commandTable[i].description;
        desc += '\n';
    }

    desc += "\nMore information about each command can be obtained by running\n" \
        "  qtresources <command> --help";

    QCommandLineParser clp;
    clp.setApplicationDescription(u"\n"_s + QCoreApplication::applicationName() + QString::fromLatin1(desc));

    clp.addHelpOption();
    clp.addVersionOption();

    clp.addPositionalArgument(u"command"_s, u"The command to execute."_s);

    // ignore unknown options for now -- the sub-commands may need them later
    clp.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsPositionalArguments);

    if (!clp.parse(QCoreApplication::arguments())) {
        fprintf(stderr, "%s\n", qPrintable(clp.errorText()));
        exit(1);
    }
    clp.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsOptions);

    switch (command(clp)) {
    default:
    case NoCommand:
        if (clp.isSet(u"version"_s))
            clp.showVersion();
        if (clp.isSet(u"help"_s))
            clp.showHelp();
        clp.showHelp(1);
        break;

    case Verify: {
        clp.addPositionalArgument(u"file"_s, u"The file name of the Qt resource file or library."_s);
        clp.process(a);

        if (clp.positionalArguments().size() != 2)
            clp.showHelp(1);

        if (loadResource(clp.positionalArguments().at(1)))
            return 0;
        break;
    }
    case List: {
        clp.addOption({{ u"a"_s }, u"Also show Qt private content."_s });
        clp.addOption({{ u"l"_s },  u"Use a long listing format."_s });
        clp.addPositionalArgument(u"<file>"_s, u"The file name of the Qt resource file or library."_s);
        clp.process(a);

        if (clp.positionalArguments().size() != 2)
            clp.showHelp(1);

        bool showQtPrivate = clp.isSet(u"a"_s);
        bool printSize = clp.isSet(u"l"_s);

        if (loadResource(clp.positionalArguments().at(1))) {
            QDirIterator it(u":"_s, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QFileInfo fi = it.nextFileInfo();
                const QString fn = fi.absoluteFilePath();

                if (showQtPrivate || !fn.startsWith(u":/qt-project.org"_s)) {
                    if (printSize) {
                        fprintf(stdout, "%c  %10lld  %s\n",
                                fi.isDir() ? 'd' : '-', fi.size(), qPrintable(fn));
                    } else {
                        fprintf(stdout, "%s\n", qPrintable(fn));
                    }
                }
            }
            return 0;
        }
        break;
    }
    case Cat: {
        clp.addPositionalArgument(u"<file>"_s, u"The file name of the Qt resource file or library."_s);
        clp.addPositionalArgument(u"<resource>"_s, u"The Qt resource file to dump."_s);
        clp.process(a);

        if (clp.positionalArguments().size() != 3)
            clp.showHelp(1);

        if (loadResource(clp.positionalArguments().at(1))) {
            QString resource = clp.positionalArguments().at(2);
            if (resource.startsWith(u"qrc:/"))
                resource = resource.mid(3);
            if (!resource.startsWith(u":/"))
                resource = u":/" + resource;
            QFile f(resource);
            if (f.open(QIODevice::ReadOnly)) {
                const QByteArray contents = f.readAll().constData();
                write(1, contents.constData(), contents.size());
                return 0;
            }
            fprintf(stderr, "Failed to open file %s: %s\n",
                    qPrintable(resource), qPrintable(f.errorString()));
        }
        break;
    }
    }
    return 2;
}

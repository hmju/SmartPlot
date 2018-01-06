#include "cloc_handler.h"

#include "utility.h"

cloc_handler::cloc_handler()
{
}

void cloc_handler::addToSystemMenu(QMenu *menu)
{
    menu->addAction( QIcon(":/graphics/genericData.png"), tr("&OpenClocFile"), this, SLOT(menuDataImport()) );
}

QPushButton *cloc_handler::addToMessageBox(QMessageBox &msgBox)
{
    return msgBox.addButton(tr("&OpenClocFile"), QMessageBox::ActionRole);
}

//Can probably make this generic
void cloc_handler::addToContextMenu(QMenu *menu, QCustomPlot* plot)
{
    if( metaData.isEmpty() || (plot->selectedPlottables().size() > 0) )
        return;

    QVariantMap menuActionMap;
    menuActionMap["Active Plot"] = qVariantFromValue( (void *)plot);

    QMenu *clocMenu = menu->addMenu( tr("CLOC") );
    clocMenu->installEventFilter(this);

    menuActionMap["Key Value"] = "ALL";
    menuActionMap["Data Storage"] = std::numeric_limits<int>::max();
    clocMenu->addAction( tr("All Languages"), this, SLOT(dataPlot()))->setData(menuActionMap);

    menuActionMap["Key Value"] = "NONE";
    menuActionMap["Data Storage"] = std::numeric_limits<int>::max();
    clocMenu->addAction( tr("No Languages"), this, SLOT(dataPlot()))->setData(menuActionMap);

    clocMenu->addSeparator();

    //Iterate through meta data and append menus as needed
    for(int metaDataIndex = 0; metaDataIndex < metaData.size(); metaDataIndex++)
    {
        menuActionMap = metaData.value(metaDataIndex);
        menuActionMap["Active Plot"] = qVariantFromValue( (void *)plot);

        QAction* action = clocMenu->addAction(QIcon(":/graphics/visible.png"), menuActionMap.value("Key Field").toString(), this, SLOT(dataPlot()));
        action->setData(menuActionMap);

        if(menuActionMap.value("Active") == false)
            action->setIconVisibleInMenu(false);
    }
}

void cloc_handler::dataImport(QVariantMap modifier)
{
    QFileInfo fileName = QFileInfo(modifier.value("File Name").toString());
    if(fileName.exists())
    {
        metaData.clear();
        connectToClocDatabase(&clocData);

        openDelimitedFile(fileName.absoluteFilePath());
    }
}

void cloc_handler::updateAxis(QCustomPlot *plot)
{
}

bool cloc_handler::eventFilter(QObject* object,QEvent* event)
{
    if ( event->type() == QEvent::MouseButtonRelease )
    {
        QMenu *objectMenu = qobject_cast<QMenu*>(object);

        //Check if object is actually a menu
        if(objectMenu != NULL)
        {
            QAction *menuAction = objectMenu->activeAction();

            //Check if the selected item has an action
            if(menuAction != NULL)
            {
                objectMenu->activeAction()->trigger();

                //The events menu has icons that will need to be updated after action is triggered
                foreach(QAction* action, objectMenu->actions())
                {
                    QVariantMap actionData = action->data().toMap();
                    for(int metaDataIndex = 0; metaDataIndex < metaData.size(); metaDataIndex++)
                    {
                        QVariantMap mData = metaData.value(metaDataIndex);
                        if( (mData.value("Key Value") == actionData.value("Key Value")) && (mData.value("Key Field") == actionData.value("Key Field")) )
                            action->setIconVisibleInMenu(mData.value("Active").toBool());
                    }
                }
                objectMenu->update();

                //Returning true prevents the standard event processing and keeps the menu open
                return true;
            }
        }
    }
    return false;
}

void cloc_handler::menuDataImport()
{
    QSettings settings;
    QVariantMap modifier;

    QString fileName = QFileDialog::getOpenFileName (NULL, tr("Open CLOC results file"),
                                                     settings.value("CLOC Handler Source Directory").toString(), "TEXT (*.txt);;ANY (*.*)");
    if(!fileName.isEmpty())
    {
        //Remember directory
        settings.setValue("CLOC Handler Source Directory", QFileInfo(fileName).absolutePath());
        modifier["File Name"] = fileName;

        dataImport(modifier);
    }
}

void cloc_handler::dataPlot()
{
    // make sure this slot is really called by a context menu action, so it carries the data we need
    if (QAction* contextAction = qobject_cast<QAction*>(sender()))
    {
        QVariantMap selectionData = contextAction->data().toMap();
        QCustomPlot* customPlot = (QCustomPlot*)selectionData["Active Plot"].value<void *>();

        for(int metaDataIndex = 0; metaDataIndex < metaData.size(); metaDataIndex++)
        {
            QVariantMap mData = metaData.value(metaDataIndex);

            if(selectionData.value("Key Value") == "ALL")
            {
                mData["Active"] = true;
            }
            else if (selectionData.value("Key Value") == "NONE")
            {
                mData["Active"] = false;
            }
            else if( mData.value("Key Value") == selectionData.value("Key Value") )
            {
                if(mData.value("Active") == true)
                {
                    mData["Active"] = false;
                }
                else
                {
                    mData["Active"] = true;
                }
            }
            metaData[metaDataIndex] = mData;
        }

        QSqlQuery query(clocData);
        bool ok;
        int numberOfVersions = 0;
        int resolution = 0x7FFFFFFF;
        double dateTime;

        QVector <double> sameLines, modifiedLines, addedLines, removedLines, dates;

        //Get number of versions
        query.exec("SELECT MAX(id) FROM snapshots");
        query.next();
        numberOfVersions = query.value(0).toInt();

        //Left justify the legend as data will probably increase left to right
        customPlot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignLeft|Qt::AlignTop);

        //Label Axes
        customPlot->xAxis->setLabel(tr("Date"));
        customPlot->yAxis->setLabel(tr("Lines of Code"));

        //Determine the intervals of the data
        query.exec(QString("SELECT target_timestamp FROM snapshots"));
        while(query.next())
        {
            static double prevDateTime = 0;
            dateTime = query.value(0).toInt();

            if( (prevDateTime != 0) && (qAbs(prevDateTime - dateTime) < resolution) && (qAbs(prevDateTime - dateTime) >= 86400) )
                resolution = qAbs(prevDateTime - dateTime);

            prevDateTime = dateTime;
        }

        //Round up to nearest day
        if(resolution%86400)
            resolution += 86400 - (resolution%86400);

        for (int version = numberOfVersions ; version > 0 ; version-- )
        {
            //Get date of version
            query.exec(QString("SELECT target_timestamp FROM snapshots WHERE id = %1").arg(version));
            query.next();
            dateTime = query.value(0).toInt();

            //Fill in gaps here
            if( (dates.size() > 0) && ((dateTime - dates.last()) > (resolution*1.1)) )
            {
                while( (dateTime - dates.last()) > (resolution*1.1) )
                {
                    dates.append(dates.last() + resolution );
                    sameLines.append(sameLines.last() + modifiedLines.last() + addedLines.last());

                    modifiedLines.append(0);
                    addedLines.append(0);
                    removedLines.append(0);
                }
            }

            //Add date stamp to the dates vector
            dates.append(dateTime);
            sameLines.append(0);
            modifiedLines.append(0);
            addedLines.append(0);
            removedLines.append(0);

            for(int metaDataIndex = 0; metaDataIndex < metaData.size(); metaDataIndex++)
            {
                QVariantMap keyFieldMetaData = metaData[metaDataIndex];
                if (keyFieldMetaData.value("Active")==true)
                {
                    query.exec(QString("SELECT nCode FROM results WHERE snapshot_id=%1 AND type='count' AND language='%2'").arg(version).arg(keyFieldMetaData.value("Key Field").toString()));
                    query.next();
                    if (query.isValid())
                        sameLines.last() = sameLines.last() + query.value(0).toInt(&ok);

                    query.exec(QString("SELECT nCode FROM results WHERE snapshot_id=%1 AND type='modified' AND language='%2'").arg(version).arg(keyFieldMetaData.value("Key Field").toString()));
                    query.next();
                    if (query.isValid())
                        modifiedLines.last() = modifiedLines.last() + query.value(0).toInt(&ok);

                    query.exec(QString("SELECT nCode FROM results WHERE snapshot_id=%1 AND type='removed' AND language='%2'").arg(version).arg(keyFieldMetaData.value("Key Field").toString()));
                    query.next();
                    if (query.isValid())
                        removedLines.last() = removedLines.last() - query.value(0).toInt(&ok);

                    query.exec(QString("SELECT nCode FROM results WHERE snapshot_id=%1 AND type='added' AND language='%2'").arg(version).arg(keyFieldMetaData.value("Key Field").toString()));
                    query.next();
                    if (query.isValid())
                        addedLines.last() = addedLines.last() + query.value(0).toInt(&ok);
                }
            }
        }

        customPlot->clearGraphs();
        customPlot->clearPlottables();
        customPlot->clearItems();

        QCPBars *same = new QCPBars(customPlot->xAxis, customPlot->yAxis);
        QCPBars *modified = new QCPBars(customPlot->xAxis, customPlot->yAxis);
        QCPBars *added = new QCPBars(customPlot->xAxis, customPlot->yAxis);
        QCPBars *removed = new QCPBars(customPlot->xAxis, customPlot->yAxis);

        removed->setName(tr("Removed"));
        removed->setData(dates, removedLines);
        removed->setBrush(Qt::darkGray);
        removed->setWidth(resolution);

        same->setName(tr("Same"));
        same->setData(dates, sameLines);
        same->setBrush(Qt::green);
        same->setWidth(resolution);

        modified->setName(tr("Modified"));
        modified->setData(dates, modifiedLines);
        modified->setBrush(Qt::yellow);
        modified->setWidth(resolution);

        added->setName(tr("Added"));
        added->setData(dates, addedLines);
        added->setBrush(Qt::red);
        added->setWidth(resolution);

        //Stack Bars
        added->moveAbove(modified);

//        customPlot->xAxis->setDateTimeFormat("MMM yyyy");
        QSharedPointer<QCPAxisTickerDateTime> ticker(new QCPAxisTickerDateTime);
        customPlot->xAxis->setTicker(ticker);

        customPlot->plottable()->rescaleAxes();
        for(int plotIndex = 0; plotIndex < customPlot->plottableCount(); plotIndex++)
        {
            customPlot->plottable(plotIndex)->rescaleAxes(true);
        }
        customPlot->replot();
        //ah.updateGraphAxes(customPlot);
    }
}

void cloc_handler::dataExport(QVariantMap modifier)
{
    Q_UNUSED(modifier);
}

void cloc_handler::openDelimitedFile(QString fileName)
{
    QFile file(fileName);
    QString line = QString();

    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream textStream(&file);
        textStream.setAutoDetectUnicode(true);

        QProgressDialog progress(tr("Importing Data"), tr("Cancel"), 0, file.size(), NULL, (Qt::WindowCloseButtonHint | Qt::WindowStaysOnTopHint));
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);

        //Open connection to database
        QSqlQuery query(clocData);

        while (!textStream.atEnd() && !progress.wasCanceled())
        {
            line = QString(textStream.readLine());
            progress.setValue(file.pos());

            if (!line.isEmpty() && !line.startsWith(QChar::Null))
            {
                query.exec(line);
            }
        }
        file.close();

        //TODO: Treat Languages like ("Unique Event Meta Data")?
        query.exec("SELECT DISTINCT(language) FROM results");
        while(query.next())
        {
            if (query.value(0).toString() == "SUM")
                continue;

            QVariantMap variantMap;
            variantMap["Key Field"] = query.value(0).toString();
            variantMap["Key Value"] = query.value(0).toString();
            variantMap["Active"] = false;
            metaData.append(variantMap);
        }
    }
}

void cloc_handler::connectToClocDatabase(QSqlDatabase *clocData)
{
    //Create blank database
    *clocData = QSqlDatabase::addDatabase("QSQLITE", "clocData");
    //Store the data in RAM
    clocData->setDatabaseName(":memory:");
    //Make the connection
    clocData->open();
}
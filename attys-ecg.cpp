/***************************************************************************
 *   Copyright (C) 2003 by Matthias H. Hennig                              *
 *   hennig@cn.stir.ac.uk                                                  *
 *   Copyright (C) 2005-2018 by Bernd Porr                                 *
 *   mail@berndporr.me.uk                                                  *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "attys-ecg.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFileDialog>
#include <QTextStream>
#include <QComboBox>
#include <QTimer>
#include <QApplication>
#include <QDesktopWidget>
#include <QMessageBox>

#include "AttysComm.h"
#include "AttysScan.h"

MainWindow::MainWindow(QWidget *parent) :
	QWidget(parent) {

	attysScan.attysComm[0]->setAdc_samplingrate_index(AttysComm::ADC_RATE_250HZ);
	sampling_rate = attysScan.attysComm[0]->getSamplingRateInHz();

	rr_det = new ECG_rr_det(sampling_rate);
	bPMCallback = new BPMCallback(this);
	rr_det->setRrListener(bPMCallback);

	attysCallback = new AttysCallback(this);
	attysScan.attysComm[0]->registerCallback(attysCallback);

	// set the PGA to max gain
	attysScan.attysComm[0]->setAdc0_gain_index(AttysComm::ADC_GAIN_6);

	// connect both channels so that we can use just 3 electrodes
	attysScan.attysComm[0]->setAdc0_mux_index(AttysComm::ADC_MUX_ECG_EINTHOVEN);
	attysScan.attysComm[0]->setAdc1_mux_index(AttysComm::ADC_MUX_ECG_EINTHOVEN);

	// 50Hz or 60Hz mains notch filter
	iirnotch1 = new Iir::Butterworth::BandStop<IIRORDER>;
	assert(iirnotch1 != NULL);
	iirnotch2 = new Iir::Butterworth::BandStop<IIRORDER>;
	assert(iirnotch2 != NULL);
	// we set it to 50Hz initially
	setNotch(50);

	// highpass
	iirhp1 = new Iir::Butterworth::HighPass<2>;
	assert(iirhp1 != NULL);
	iirhp1->setup(IIRORDER, sampling_rate, 0.5);
	iirhp2 = new Iir::Butterworth::HighPass<2>;
	assert(iirhp2 != NULL);
	iirhp2->setup(IIRORDER, sampling_rate, 0.5);

	char styleSheet[] = "padding:0px;margin:0px;border:0px;";

	QHBoxLayout *mainLayout = new QHBoxLayout(this);

	QVBoxLayout *controlLayout = new QVBoxLayout;
	controlLayout->setAlignment(Qt::AlignTop);

	mainLayout->addLayout(controlLayout);

	QVBoxLayout *plotLayout = new QVBoxLayout;

	mainLayout->addLayout(plotLayout);

	mainLayout->setStretchFactor(controlLayout, 1);
	mainLayout->setStretchFactor(plotLayout, 4);

	double maxTime = 5;
	double minRange = -2;
	double maxRange = 2;
	const char* xlabel = "t/s";
	QDesktopWidget *mydesktop = QApplication::desktop();
	int h = mydesktop->height() / 5;
	int w = mydesktop->width();
	dataPlotI = new DataPlot(maxTime,
		sampling_rate,
		minRange,
		maxRange,
		"Einthoven I",
		xlabel,
		"I/mV",
		this);
	dataPlotI->setMaximumSize(w, h);
	dataPlotI->setStyleSheet(styleSheet);
	plotLayout->addWidget(dataPlotI);
	dataPlotI->show();

	dataPlotII = new DataPlot(maxTime,
		sampling_rate,
		minRange,
		maxRange,
		"Einthoven II",
		xlabel,
		"II/mV",
		this);
	dataPlotII->setMaximumSize(w, h);
	dataPlotII->setStyleSheet(styleSheet);
	plotLayout->addWidget(dataPlotII);
	dataPlotII->show();

	dataPlotIII = new DataPlot(maxTime,
		sampling_rate,
		minRange,
		maxRange,
		"Einthoven III",
		xlabel,
		"III/mV",
		this);
	dataPlotIII->setMaximumSize(w, h);
	dataPlotIII->setStyleSheet(styleSheet);
	plotLayout->addWidget(dataPlotIII);
	dataPlotIII->show();

	plotLayout->addSpacing(20);

	dataPlotBPM = new DataPlot(120,
		1,
		0,
		200,
		"Heartrate",
		"RR number",
		"Heartrate/bpm",
		this);
	dataPlotBPM->setMaximumSize(w, h);
	dataPlotBPM->setStyleSheet(styleSheet);
	plotLayout->addWidget(dataPlotBPM);
	dataPlotBPM->show();


	/*---- Buttons ----*/

	QGroupBox   *ECGfunGroup = new QGroupBox("ECG", this);
	QVBoxLayout *ecgFunLayout = new QVBoxLayout;
	ecgFunLayout->setAlignment(Qt::AlignTop);
	ECGfunGroup->setLayout(ecgFunLayout);
	ECGfunGroup->setAlignment(Qt::AlignJustify);
	ECGfunGroup->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
	controlLayout->addWidget(ECGfunGroup);

	notchFreq = new QComboBox(ECGfunGroup);
	notchFreq->addItem(tr("50Hz bandstop"));
	notchFreq->addItem(tr("60Hz bandstop"));
	ecgFunLayout->addWidget(notchFreq);
	connect(notchFreq, SIGNAL(currentIndexChanged(int)), SLOT(slotSelectNotchFreq(int)));

	yRange = new QComboBox(ECGfunGroup);
	yRange->addItem(tr("1mV"),1.0);
	yRange->addItem(tr("1.5mV"),1.5);
	yRange->addItem(tr("2mV"),2.0);
	ecgFunLayout->addWidget(yRange);
	connect(yRange, SIGNAL(currentIndexChanged(int)), SLOT(slotSelectYrange(int)));
	yRange->setCurrentIndex(2);

	QGroupBox   *ECGfileGroup = new QGroupBox("file ops", this);
	QVBoxLayout *ecgfileLayout = new QVBoxLayout;
	ecgfileLayout->setAlignment(Qt::AlignTop);
	ECGfileGroup->setLayout(ecgfileLayout);
	ECGfileGroup->setAlignment(Qt::AlignJustify);
	ECGfileGroup->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
	controlLayout->addWidget(ECGfileGroup);

	saveECG = new QPushButton(ECGfileGroup);
	saveECG->setText("Filename");
	ecgfileLayout->addWidget(saveECG);
	connect(saveECG, SIGNAL(clicked()), SLOT(slotSaveECG()));

	recordECG = new QPushButton(ECGfileGroup);
	recordECG->setText("Record on/off");
	recordECG->setCheckable(true);
	recordECG->setEnabled(false);
	ecgfileLayout->addWidget(recordECG);
	connect(recordECG, SIGNAL(clicked()), SLOT(slotRecordECG()));

	statusLabel = new QLabel(ECGfileGroup);
	ecgfileLayout->addWidget(statusLabel);

	QGroupBox   *ECGbpmGroup = new QGroupBox("Heartrate", this);
	QVBoxLayout *ecgbpmLayout = new QVBoxLayout;
	ecgbpmLayout->setAlignment(Qt::AlignTop);
	ECGbpmGroup->setLayout(ecgbpmLayout);
	ECGbpmGroup->setAlignment(Qt::AlignJustify);
	ECGbpmGroup->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
	controlLayout->addWidget(ECGbpmGroup);

	clearBPM = new QPushButton(ECGbpmGroup);
	clearBPM->setText("clear BPM plot");
	ecgbpmLayout->addWidget(clearBPM);
	connect(clearBPM, SIGNAL(clicked()), SLOT(slotClearBPM()));

	bpmLabel = new QLabel(ECGbpmGroup);
	ecgbpmLayout->addWidget(bpmLabel);
	bpmLabel->setFont(QFont("Courier", 18));

	// Generate timer event every 50ms
	startTimer(50);

	attysScan.attysComm[0]->start();
}

MainWindow::~MainWindow()
{
	attysScan.attysComm[0]->unregisterCallback();
	attysScan.attysComm[0]->quit();
	if (ecgFile) {
		fclose(ecgFile);
	}
}

void MainWindow::slotSaveECG()
{
	if (recordingOn) return;
	QString filters(tr("tab separated values (*.tsv)"));
	QFileDialog dialog(this);
	dialog.setFileMode(QFileDialog::AnyFile);
	dialog.setNameFilter(filters);
	dialog.setViewMode(QFileDialog::Detail);

	if (dialog.exec()) {
		fileName = dialog.selectedFiles()[0];
		if (!fileName.isNull()) {
			QString extension = dialog.selectedNameFilter();
			extension = extension.mid(extension.indexOf("."), 4);
			if (fileName.indexOf(extension) == -1) {
				fileName = fileName + extension;
			}
			ecgFile = fopen(fileName.toLocal8Bit().constData(), "wt");
			if (ecgFile) {
				recordECG->setEnabled(true);
			}
			else {
				QMessageBox msgbox;
				msgbox.setText("Could not save to "+ fileName);
				msgbox.exec();
				fileName = "";
			}
		}
	}
}

void MainWindow::setNotch(double f) {
	iirnotch1->setup(IIRORDER, sampling_rate, f, 2.5);
	iirnotch2->setup(IIRORDER, sampling_rate, f, 2.5);
}

void MainWindow::slotSelectNotchFreq(int f) {
	switch (f) {
	case 0:
		setNotch(50);
		break;
	case 1:
		setNotch(60);
		break;
	}
}

void MainWindow::slotSelectYrange(int r) {
	double y = 1.0;
	switch (r) {
	case 0:
		y = 1.0;
		break;
	case 1:
		y = 1.5;
		break;
	case 2:
		y = 2.0;
		break;
	}
	dataPlotI->setYScale(-y, y);
	dataPlotII->setYScale(-y, y);
	dataPlotIII->setYScale(-y, y);
}

void MainWindow::slotClearBPM()
{
	dataPlotBPM->reset();
}

void MainWindow::slotRecordECG()
{
	if (recordingOn && (!(recordECG->isChecked()))) {
		recordECG->setEnabled(false);
	}
	recordingOn = recordECG->isChecked();
	tRec = 0;
	if (recordingOn) {
		setWindowTitle(QString("attys-ecg: recording ") + fileName);
	}
	else {
		setWindowTitle(QString("attys-ecg"));
		saveFileMutex.lock();
		if (ecgFile) {
			fclose(ecgFile);
			ecgFile = NULL;
		}
		saveFileMutex.unlock();
	}
}

void MainWindow::timerEvent(QTimerEvent *) {
	dataPlotI->replot();
	dataPlotII->replot();
	dataPlotIII->replot();
	dataPlotBPM->replot();
	if (recordingOn) {
		QString tRecString = QString::number(((int)tRec));
		statusLabel->setText("Rec: t=" + tRecString+" sec");
	}
	else {
		statusLabel->setText("");
	}
}


void MainWindow::hasData(float, float *sample)
{
	double y1 = sample[AttysComm::INDEX_Analogue_channel_1];
	double y2 = sample[AttysComm::INDEX_Analogue_channel_2];

	// highpass filtering of the data
	y1 = iirhp1->filter(y1);
	y2 = iirhp2->filter(y2);

	// removing 50Hz notch
	II = iirnotch1->filter(y1);
	III = iirnotch2->filter(y2);

	rr_det->detect(II);

	I = II - III;
	aVR = III / 2 - II;
	aVL = II / 2 - III;
	aVF = II / 2 + III / 2;

	// plot the data
	const double scaling = 1000;
	dataPlotI->setNewData(I*scaling);
	dataPlotII->setNewData(II*scaling);
	dataPlotIII->setNewData(III*scaling);

	// Are we recording?
	saveFileMutex.lock();
	if (ecgFile)
	{
		char s = '\t';
		double t = (double)sampleNumber / sampling_rate;
		fprintf(ecgFile, "%e%c", t, s);
		fprintf(ecgFile, "%e%c", I, s);
		fprintf(ecgFile, "%e%c", II, s);
		fprintf(ecgFile, "%e%c", III, s);
		fprintf(ecgFile, "%e%c", aVR, s);
		fprintf(ecgFile, "%e%c", aVL, s);
		fprintf(ecgFile, "%e%c", aVF, s);
		fprintf(ecgFile, "%f\n", bpm);
		bpm = 0;
	}
	saveFileMutex.unlock();
    
	sampleNumber++;
	tRec = tRec + 1.0 / sampling_rate;
}

void  MainWindow::hasRpeak(long,
			   float filtBpm,
			   float,
			   double,
			   double) {
	//fprintf(stderr,"BPM = %f\n",filtBpm);
	bpm = filtBpm;
	dataPlotBPM->setNewData(bpm);
	char tmp[16];
	sprintf(tmp,"%03d BPM",(int)bpm);
	bpmLabel->setText(tmp);
}

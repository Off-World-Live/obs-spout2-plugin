/**
 * Copyright Off World Live Ltd (https://offworld.live), 2019-2021
 *
 * and licenced under the GPL v2 (https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
 *
 * Many thanks to authors of https://github.com/baffler/OBS-OpenVR-Input-Plugin which
 * was used as guidance to working with the OBS Studio APIs
 */

#include "win-spout-output-settings.h"
#include "ui_win-spout-output-settings.h"
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include "../win-spout-config.h"
#include "../win-spout.h"

extern win_spout_output_settings *spout_output_settings;

win_spout_output_settings::win_spout_output_settings(QWidget *parent)
	: QDialog(parent),
	  ui(new Ui::win_spout_output_settings)
{
	this->setAttribute(Qt::WA_DeleteOnClose);
	ui->setupUi(this);
	connect(ui->pushButton_start, SIGNAL(clicked(bool)), this, SLOT(on_start()));
	connect(ui->pushButton_stop, SIGNAL(clicked(bool)), this, SLOT(on_stop()));

	win_spout_config *config = win_spout_config::get();

	ui->checkBox_auto->setChecked(config->auto_start);
	ui->checkBox_continuous->setChecked(config->continuous_broadcast);
	ui->lineEdit_spoutname->setText(config->spout_output_name);

	set_started_button_state(!spout_output_is_active());
}

void win_spout_output_settings::save_settings()
{
	win_spout_config *config = win_spout_config::get();
	config->auto_start = ui->checkBox_auto->isChecked();
	config->continuous_broadcast = ui->checkBox_continuous->isChecked();
	config->spout_output_name = ui->lineEdit_spoutname->text();
	win_spout_config::get()->save();
}

win_spout_output_settings::~win_spout_output_settings()
{
	save_settings();
	if (spout_output_settings == this) {
		spout_output_settings = nullptr;
	}
	delete ui;
}

void win_spout_output_settings::on_start()
{
	QByteArray spout_output_name = ui->lineEdit_spoutname->text().toUtf8();
	set_started_button_state(false);
	save_settings();
	spout_output_start(spout_output_name);
}

void win_spout_output_settings::on_stop()
{
	set_started_button_state(true);
	spout_output_stop();
}

void win_spout_output_settings::set_started_button_state(bool started)
{
	ui->pushButton_start->setEnabled(started);
	ui->pushButton_stop->setEnabled(!started);
}

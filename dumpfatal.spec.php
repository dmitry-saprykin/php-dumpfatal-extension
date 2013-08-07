<?php

if( !extension_loaded("dumpfatal") ) {

    /*
     * INI_ENTRIES
     *
     * dupmpfatal.enabled
     *
     * Если не указано в php.ini, значение по умолчанию - 0
     *
     * ini_set('dupmpfatal.enabled', 1);
     * - если модуль до этого был отключен
     * - будет сгенерирована копия трейса и модуль начнет слежение за выполнением скрипта
     *
     * ini_set('dupmpfatal.enabled', 0);
     * - если модуль до этого был включен
     * - модуль сбросить текущий трейс и перестанет следить за выполнением скрипта.
     * - накладные расходы ~0
     */

    /**
     * Получить текущее состояние трейса.
     *
     * @return array
     */
    function dumpfatal_gettrace(){}

    /**
     * Установить дополнительную информацию, выводимую вместе с трейсом в случае фатала.
     *
     * @param string $info
     *
     * @return bool
     */
    function dumpfatal_set_aditional_info($info) {}

}
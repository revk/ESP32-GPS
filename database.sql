-- MySQL dump 10.17  Distrib 10.3.18-MariaDB, for debian-linux-gnu (x86_64)
--
-- Host: localhost    Database: gps
-- ------------------------------------------------------
-- Server version	10.3.18-MariaDB-0+deb10u1

/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8mb4 */;
/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;
/*!40103 SET TIME_ZONE='+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;

--
-- Table structure for table `auth`
--

DROP TABLE IF EXISTS `auth`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `auth` (
  `ID` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `replaces` int(10) unsigned DEFAULT NULL,
  `issued` datetime NOT NULL,
  `device` int(10) unsigned NOT NULL,
  `aes` char(32) NOT NULL,
  `auth` text NOT NULL,
  PRIMARY KEY (`ID`),
  KEY `device` (`device`),
  KEY `auth_ibfk_1` (`replaces`),
  CONSTRAINT `auth_ibfk_1` FOREIGN KEY (`replaces`) REFERENCES `auth` (`ID`) ON DELETE CASCADE,
  CONSTRAINT `auth_ibfk_2` FOREIGN KEY (`device`) REFERENCES `device` (`ID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `device`
--

DROP TABLE IF EXISTS `device`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `device` (
  `ID` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `tag` varchar(6) DEFAULT NULL,
  `auth` int(10) unsigned DEFAULT NULL,
  `lastupdateutc` datetime DEFAULT NULL,
  `nextupdateutc` datetime DEFAULT NULL,
  `lastfixutc` datetime(1) DEFAULT NULL,
  `lastip` datetime DEFAULT NULL,
  `lat` decimal(19,9) DEFAULT NULL,
  `lon` decimal(14,9) DEFAULT NULL,
  `alt` decimal(8,1) DEFAULT NULL,
  `ecefx` decimal(14,6) DEFAULT NULL,
  `ecefy` decimal(14,6) DEFAULT NULL,
  `ecefz` decimal(14,6) DEFAULT NULL,
  `ip` varchar(39) DEFAULT NULL,
  `port` int(10) unsigned DEFAULT NULL,
  `mqtt` datetime DEFAULT NULL,
  `version` text DEFAULT NULL,
  `iccid` text DEFAULT NULL,
  `imei` text DEFAULT NULL,
  `upgrade` enum('N','Y') NOT NULL DEFAULT 'N',
  PRIMARY KEY (`ID`),
  UNIQUE KEY `device` (`tag`),
  KEY `ip` (`ip`,`port`),
  KEY `auth` (`auth`),
  CONSTRAINT `device_ibfk_1` FOREIGN KEY (`auth`) REFERENCES `auth` (`ID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `gps`
--

DROP TABLE IF EXISTS `gps`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `gps` (
  `ID` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `utc` datetime(1) NOT NULL,
  `device` int(10) unsigned NOT NULL,
  `lat` decimal(19,9) DEFAULT NULL,
  `lon` decimal(14,9) DEFAULT NULL,
  `alt` decimal(8,1) DEFAULT NULL,
  `ecefx` decimal(14,6) DEFAULT NULL,
  `ecefy` decimal(14,6) DEFAULT NULL,
  `ecefz` decimal(14,6) DEFAULT NULL,
  `sats` int(2) DEFAULT NULL,
  `hepe` decimal(4,1) DEFAULT NULL,
  PRIMARY KEY (`ID`),
  UNIQUE KEY `utc_2` (`utc`,`device`),
  KEY `utc` (`utc`),
  KEY `device` (`device`),
  CONSTRAINT `gps_ibfk_1` FOREIGN KEY (`device`) REFERENCES `device` (`ID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `log`
--

DROP TABLE IF EXISTS `log`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `log` (
  `ID` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `device` int(10) unsigned NOT NULL,
  `received` datetime NOT NULL,
  `utc` datetime NOT NULL,
  `endutc` datetime DEFAULT NULL,
  `fixes` int(10) unsigned DEFAULT NULL,
  `ip` varchar(39) DEFAULT NULL,
  `port` int(5) DEFAULT NULL,
  `margin` decimal(8,3) DEFAULT NULL,
  `tempc` decimal(5,1) DEFAULT NULL,
  PRIMARY KEY (`ID`),
  KEY `device` (`device`),
  CONSTRAINT `log_ibfk_1` FOREIGN KEY (`device`) REFERENCES `device` (`ID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;

-- Dump completed on 2019-11-25 12:19:52
